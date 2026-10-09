#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

"""
Runs rules over a file: collects violations, computes the fixed content and
splits the difference into hunks that can be applied selectively.

Inline suppressions (any comment style):

    /* mynewt-format: off */ ... /* mynewt-format: on */
        Nothing between the markers is reported or changed.

    code;  /* mynewt-format: ignore */
    code;  /* mynewt-format: ignore line-length, no-tabs */
        Suppress all (or the listed) rules on this line.

    /* mynewt-format: ignore-next-line [rules] */
        Same, for the following line.

    /* clang-format off */ ... /* clang-format on */
        Honoured as well: regions excluded from formatting before this tool
        existed (generated tables, vendor code) stay excluded.
"""

from __future__ import annotations

import difflib
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

from . import cparse
from .core import FileContext, Rule, Violation

ALL = "*"

_DIRECTIVE_RE = re.compile(
    r"mynewt-format:\s*(off|on|ignore-next-line|ignore)\b([\w\-, \t]*)"
    r"|clang-format\s+(off|on)\b")


def parse_suppressions(lines: List[str]) -> Dict[int, Set[str]]:
    """Map 1-based line -> set of suppressed rule names ('*' = all)."""
    out: Dict[int, Set[str]] = {}
    off = False
    for ln, text in enumerate(lines, 1):
        m = _DIRECTIVE_RE.search(text)
        kind = (m.group(1) or m.group(3)) if m else None
        if kind == "off":
            off = True
        if off:
            out.setdefault(ln, set()).add(ALL)
        if kind == "on":
            off = False
        if kind in ("ignore", "ignore-next-line"):
            names = {n for n in re.split(r"[\s,]+", m.group(2).strip()) if n} or {ALL}
            target = ln + 1 if kind == "ignore-next-line" else ln
            out.setdefault(target, set()).update(names)
    return out


@dataclass
class Hunk:
    """A contiguous change: original lines [a_start, a_end) -> new_lines."""
    a_start: int                 # 0-based, into the original line list
    a_end: int
    new_lines: List[str]         # with line endings
    violations: List[Violation] = field(default_factory=list)

    def touches(self, line_numbers: Set[int]) -> bool:
        """True if the hunk changes (or inserts next to) any 1-based line given."""
        if self.a_start == self.a_end:
            return self.a_start in line_numbers or self.a_start + 1 in line_numbers
        return any(ln in line_numbers for ln in range(self.a_start + 1, self.a_end + 1))


def line_opcodes(a: List[str], b: List[str]):
    """
    difflib opcodes for two line lists. The common head and tail are trimmed
    first - fixes are local, so this keeps diffing fast on big files.
    """
    pre = 0
    limit = min(len(a), len(b))
    while pre < limit and a[pre] == b[pre]:
        pre += 1
    suf = 0
    while suf < limit - pre and a[len(a) - 1 - suf] == b[len(b) - 1 - suf]:
        suf += 1
    ops = []
    if pre:
        ops.append(("equal", 0, pre, 0, pre))
    sm = difflib.SequenceMatcher(None, a[pre:len(a) - suf], b[pre:len(b) - suf])
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        ops.append((tag, i1 + pre, i2 + pre, j1 + pre, j2 + pre))
    if suf:
        ops.append(("equal", len(a) - suf, len(a), len(b) - suf, len(b)))
    return ops


def compute_hunks(original: str, proposed: str) -> List[Hunk]:
    """
    Smallest independent changes between original and proposed. A block of
    N changed lines replaced by N lines is split per line (those are separate
    edits, e.g. trailing blanks on consecutive lines); blocks that change the
    number of lines (a joined signature, an added brace) stay one unit.
    """
    a = original.splitlines(keepends=True)
    b = proposed.splitlines(keepends=True)
    hunks: List[Hunk] = []
    for tag, i1, i2, j1, j2 in line_opcodes(a, b):
        if tag == "equal":
            continue
        if tag == "replace" and i2 - i1 == j2 - j1:
            hunks.extend(Hunk(i1 + k, i1 + k + 1, [b[j1 + k]]) for k in range(i2 - i1))
        else:
            hunks.append(Hunk(i1, i2, b[j1:j2]))
    return hunks


def merge_hunks(original: str, hunks: List[Hunk], gap: int = 0) -> List[Hunk]:
    """Join hunks at most 'gap' unchanged lines apart (default: touching ones)."""
    a = original.splitlines(keepends=True)
    out: List[Hunk] = []
    for h in sorted(hunks, key=lambda h: h.a_start):
        prev = out[-1] if out else None
        if prev is not None and h.a_start - prev.a_end <= gap:
            prev.new_lines = prev.new_lines + a[prev.a_end:h.a_start] + h.new_lines
            prev.a_end = h.a_end
        else:
            out.append(Hunk(h.a_start, h.a_end, list(h.new_lines)))
    return out


def apply_hunks(original: str, hunks: List[Hunk]) -> str:
    a = original.splitlines(keepends=True)
    out: List[str] = []
    pos = 0
    for h in sorted(hunks, key=lambda h: h.a_start):
        out += a[pos:h.a_start]
        out += h.new_lines
        pos = h.a_end
    out += a[pos:]
    return "".join(out)


@dataclass
class FileResult:
    path: Path
    original: str
    proposed: str                         # original + all accepted-able hunks
    violations: List[Violation]           # on the original content
    hunks: List[Hunk]
    errors: List[str] = field(default_factory=list)   # internal problems

    @property
    def changed(self) -> bool:
        return bool(self.hunks)


def _same_code(old: str, new: str, rule: Rule) -> bool:
    """
    True if old and new differ only in layout and comments - apart from the
    tokens the rule declares it may insert, and spellings it normalises.
    """
    a = cparse.strip_code_tokens(old)
    b = cparse.strip_code_tokens(new)
    if a == b:
        return True
    ignorable = rule.may_insert_tokens

    def keep(fp: str):
        return [rule.normalize_token(t) for t in fp.split("\0") if t not in ignorable]
    return keep(a) == keep(b)


class Engine:
    def __init__(self, rules: List[Rule], settings: Dict[str, Any]):
        self.rules = rules
        self.settings = settings
        self.passes = int(settings.get("fix_passes", 5))

    # -- checking -----------------------------------------------------------

    def check(self, path: Path, content: str) -> Tuple[List[Violation], List[str]]:
        """Run all rules; suppressed violations are dropped."""
        ctx = FileContext(path, content, self.settings)
        out: List[Violation] = []
        errors: List[str] = []
        for rule in self.rules:
            try:
                out.extend(rule.check(ctx))
            except Exception as exc:  # a broken rule must not kill the run
                errors.append(f"rule '{rule.name}' crashed in check(): {exc!r}")
        supp = parse_suppressions(ctx.lines)
        if supp:
            out = [v for v in out
                   if not (supp.get(v.line) and (ALL in supp[v.line] or v.rule in supp[v.line]))]
        out.sort(key=lambda v: (v.line, v.column, v.rule))
        return out, errors

    # -- fixing -------------------------------------------------------------

    def fix(self, path: Path, content: str) -> Tuple[str, List[str]]:
        """Apply all fixable rules until nothing changes (or passes run out)."""
        errors: List[str] = []
        current = content
        broken: Set[str] = set()
        for _ in range(self.passes):
            changed = False
            for rule in self.rules:
                if not rule.fixable or rule.name in broken:
                    continue
                try:
                    new = rule.fix(FileContext(path, current, self.settings))
                except Exception as exc:
                    errors.append(f"rule '{rule.name}' crashed in fix(): {exc!r}")
                    broken.add(rule.name)
                    continue
                if new is None or new == current:
                    continue
                if not _same_code(current, new, rule):
                    errors.append(f"rule '{rule.name}' produced a fix that changes code "
                                  f"tokens; fix discarded (please report this)")
                    broken.add(rule.name)
                    continue
                current = new
                changed = True
            if not changed:
                break
        return current, errors

    # -- whole file ---------------------------------------------------------

    def process(self, path: Path, content: str,
                changed_lines: Optional[Set[int]] = None) -> FileResult:
        """
        Check and fix one file. If changed_lines is given, only violations on
        those lines are reported and only hunks touching them are proposed.
        """
        violations, errors = self.check(path, content)
        proposed, fix_errors = self.fix(path, content)
        errors += fix_errors

        supp = parse_suppressions(content.split("\n"))
        if changed_lines is not None:
            violations = [v for v in violations if v.line in changed_lines]

        hunks = compute_hunks(content, proposed)
        supp_lines = set(supp)
        kept = [h for h in hunks
                if not (supp_lines and h.touches(supp_lines))
                and (changed_lines is None or h.touches(changed_lines))]
        if len(kept) != len(hunks):
            proposed = apply_hunks(content, kept)
        # Edits on consecutive lines are reviewed as one change.
        kept = merge_hunks(content, kept)
        for h in kept:
            h.violations = [v for v in violations
                            if h.a_start + 1 <= v.line <= max(h.a_end, h.a_start + 1)]
        return FileResult(path, content, proposed, violations, kept, errors)
