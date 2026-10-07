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

"""Terminal output: diagnostics, diffs and rule documentation."""

from __future__ import annotations

import shutil
import textwrap
from pathlib import Path
from typing import Iterable, List, Optional

from .config import Config
from .core import Rule, Violation


class Colors:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    RED = "\033[31m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    CYAN = "\033[36m"

    @classmethod
    def disable(cls) -> None:
        for name in ("RESET", "BOLD", "DIM", "RED", "GREEN", "YELLOW", "CYAN"):
            setattr(cls, name, "")


def display_path(path: Path, root: Optional[Path]) -> str:
    if root is not None:
        try:
            return path.resolve().relative_to(root).as_posix()
        except ValueError:
            pass
    return str(path)


def format_violation(v: Violation, root: Optional[Path], fmt: str = "text") -> str:
    path = display_path(v.path, root)
    if fmt == "github":
        kind = "error" if v.severity == "error" else "warning"
        return (f"::{kind} file={path},line={v.line},col={v.column},"
                f"title={v.rule}::{v.message}")
    sev = (f"{Colors.RED}error{Colors.RESET}" if v.severity == "error"
           else f"{Colors.YELLOW}warning{Colors.RESET}")
    fix = f" {Colors.DIM}(fixable){Colors.RESET}" if v.fixable else ""
    return (f"{Colors.BOLD}{path}:{v.line}:{v.column}:{Colors.RESET} {sev}: "
            f"{v.message} [{Colors.CYAN}{v.rule}{Colors.RESET}]{fix}")


def _source_lines(v: Violation, lines: List[str], tab_width: int) -> List[str]:
    """The offending source line with a caret under the reported column."""
    if not 1 <= v.line <= len(lines):
        return []
    text = lines[v.line - 1].rstrip("\r")
    num = str(v.line)
    pad = " " * len(num)
    caret = len(text[:max(v.column - 1, 0)].expandtabs(tab_width))
    return [f"  {Colors.DIM}{num} |{Colors.RESET} {text.expandtabs(tab_width)}",
            f"  {Colors.DIM}{pad} |{Colors.RESET} {' ' * caret}{Colors.GREEN}^{Colors.RESET}"]


def _fix_lines(hunk, lines: List[str], tab_width: int) -> List[str]:
    """A proposed fix as '-' (current) and '+' (proposed) lines."""
    out = [f"  {Colors.BOLD}suggested fix:{Colors.RESET}"]
    for line in lines[hunk.a_start:hunk.a_end]:
        out.append(f"  {Colors.RED}- {line.rstrip(chr(13)).expandtabs(tab_width)}{Colors.RESET}")
    for line in hunk.new_lines:
        text = line.rstrip("\r\n").expandtabs(tab_width)
        out.append(f"  {Colors.GREEN}+ {text}{Colors.RESET}")
    return out


def _display_hunks(hunks: list, lines: List[str]) -> list:
    """
    Split one-to-one line replacements into single-line fixes, so each fix is
    shown under its own problem; blocks that change the number of lines (a
    joined signature, added braces) stay together.
    """
    from .engine import Hunk
    out = []
    for h in hunks:
        old = lines[h.a_start:h.a_end]
        if len(old) == len(h.new_lines) > 1:
            for k, (a, b) in enumerate(zip(old, h.new_lines)):
                if a != b.rstrip("\n"):
                    out.append(Hunk(h.a_start + k, h.a_start + k + 1, [b]))
        else:
            out.append(h)
    return out


def format_problems(violations: List[Violation], content: str, hunks: list,
                    root: Optional[Path], fmt: str = "text", report: str = "code",
                    tab_width: int = 4) -> List[str]:
    """
    Render violations. report='list' gives one line per problem; report='code'
    (default) adds the source line with a caret and, for fixable problems,
    the proposed fix right below the last problem it resolves.
    """
    if report == "list":
        return [format_violation(v, root, fmt) for v in violations]
    lines = content.split("\n")
    owner = {}
    for h in _display_hunks(hunks, lines):
        for v in violations:
            if h.a_start + 1 <= v.line <= max(h.a_end, h.a_start + 1):
                owner.setdefault(id(v), h)
    last_of = {}
    for v in violations:
        if id(v) in owner:
            last_of[id(owner[id(v)])] = id(v)
    out: List[str] = []
    for v in violations:
        if out:
            out.append("")
        out.append(format_violation(v, root, fmt))
        out += _source_lines(v, lines, tab_width)
        h = owner.get(id(v))
        if h is not None and last_of.get(id(h)) == id(v):
            out += _fix_lines(h, lines, tab_width)
    return out


def colorize_diff_line(line: str) -> str:
    line = line.rstrip("\n")
    if line.startswith(("+++", "---")):
        return f"{Colors.BOLD}{line}{Colors.RESET}"
    if line.startswith("@@"):
        return f"{Colors.CYAN}{line}{Colors.RESET}"
    if line.startswith("+"):
        return f"{Colors.GREEN}{line}{Colors.RESET}"
    if line.startswith("-"):
        return f"{Colors.RED}{line}{Colors.RESET}"
    return line


def _grouped(ops, n: int):
    """difflib.SequenceMatcher.get_grouped_opcodes() for a precomputed opcode list."""
    codes = list(ops) or [("equal", 0, 1, 0, 1)]
    if codes[0][0] == "equal":
        tag, i1, i2, j1, j2 = codes[0]
        codes[0] = tag, max(i1, i2 - n), i2, max(j1, j2 - n), j2
    if codes[-1][0] == "equal":
        tag, i1, i2, j1, j2 = codes[-1]
        codes[-1] = tag, i1, min(i2, i1 + n), j1, min(j2, j1 + n)
    group = []
    for tag, i1, i2, j1, j2 in codes:
        if tag == "equal" and i2 - i1 > 2 * n:
            group.append((tag, i1, min(i2, i1 + n), j1, min(j2, j1 + n)))
            yield group
            group = []
            i1, j1 = max(i1, i2 - n), max(j1, j2 - n)
        group.append((tag, i1, i2, j1, j2))
    if group and not (len(group) == 1 and group[0][0] == "equal"):
        yield group


def unified_diff(original: str, proposed: str, path: Path, root: Optional[Path],
                 context: int = 3) -> List[str]:
    """
    Unified diff of two versions of a file. Built on the engine's trimmed line
    matcher: difflib's own unified_diff treats very common lines ('{', '}')
    as junk in big files and reports them as changed.
    """
    from .engine import line_opcodes
    name = display_path(path, root)
    a = original.splitlines(keepends=True)
    b = proposed.splitlines(keepends=True)
    out: List[str] = []
    for group in _grouped(line_opcodes(a, b), context):
        if not out:
            out += [f"--- a/{name}\n", f"+++ b/{name}\n"]
        first, last = group[0], group[-1]
        i1, i2, j1, j2 = first[1], last[2], first[3], last[4]
        out.append(f"@@ -{_range(i1, i2)} +{_range(j1, j2)} @@\n")
        for tag, a1, a2, b1, b2 in group:
            if tag == "equal":
                out += [" " + line for line in a[a1:a2]]
                continue
            out += ["-" + line for line in a[a1:a2]]
            out += ["+" + line for line in b[b1:b2]]
    return out


def _range(start: int, stop: int) -> str:
    """Hunk range in unified diff notation (1-based start, length)."""
    length = stop - start
    if length == 1:
        return str(start + 1)
    if length == 0:
        return f"{start},0"
    return f"{start + 1},{length}"


def print_diff(lines: Iterable[str]) -> None:
    for line in lines:
        print(colorize_diff_line(line))
        if not line.endswith("\n"):
            print("\\ No newline at end of file")


def _term_width() -> int:
    return max(60, min(120, shutil.get_terminal_size((100, 20)).columns))


def list_rules(rules: List[type], config: Config, verbose: bool) -> None:
    width = _term_width()
    name_w = max(len(r.name) for r in rules) + 2
    head = f"{'RULE':<{name_w}}{'SEVERITY':<10}{'FIX':<5}{'ON':<5}SUMMARY"
    print(f"{Colors.BOLD}{head}{Colors.RESET}")
    category = None
    for cls in rules:
        if cls.category != category:
            category = cls.category
            print(f"\n{Colors.BOLD}@{category}{Colors.RESET}")
        cfg = config.rules.get(cls.name, {})
        sev = cfg.get("severity", cls.severity)
        on = config.is_enabled(cls.name)
        sev_c = Colors.RED if sev == "error" else Colors.YELLOW
        summary = textwrap.wrap(cls.summary, max(20, width - name_w - 20), break_on_hyphens=False) or [""]
        print(f"{cls.name:<{name_w}}{sev_c}{sev:<10}{Colors.RESET}"
              f"{'yes' if cls.fixable else 'no':<5}"
              f"{(Colors.GREEN + 'on ' if on else Colors.DIM + 'off')}{Colors.RESET}  "
              f"{summary[0]}")
        for extra in summary[1:]:
            print(" " * (name_w + 20) + extra)
        if verbose:
            print()
            print(textwrap.indent(explain_body(cls, config), "    "))
            print()
    print(f"\nRun with --explain RULE for the full description of a rule.")


def explain_body(cls: type, config: Config) -> str:
    out = [cls.explain_text() or cls.summary]
    if cls.options:
        cfg = config.rules.get(cls.name, {})
        out.append("")
        out.append("Options ([rules.%s] in the config file):" % cls.name)
        for key, default in cls.options.items():
            current = cfg.get(key, default)
            mark = "" if current == default else f"   (configured: {current!r})"
            out.append(f"  {key} = {default!r}{mark}")
    return "\n".join(out)


def explain_rule(cls: type, config: Config) -> None:
    cfg = config.rules.get(cls.name, {})
    sev = cfg.get("severity", cls.severity)
    state = "enabled" if config.is_enabled(cls.name) else "disabled"
    print(f"{Colors.BOLD}{cls.name}{Colors.RESET}  "
          f"(@{cls.category}, {sev}, {'auto-fixable' if cls.fixable else 'not fixable'}, "
          f"{state})")
    print(f"{cls.summary}\n")
    print(explain_body(cls, config))


def list_rulesets(config: Config) -> None:
    default = config.settings["default_ruleset"]
    names = sorted(set(config.rulesets) | {"default"})
    for name in names:
        try:
            selected = sorted(config.resolve_ruleset(name))
        except Exception as exc:  # report broken rulesets instead of dying
            selected = [f"<error: {exc}>"]
        mark = " (default)" if name == default else ""
        spec = config.rulesets.get(name, ["*"])
        print(f"{Colors.BOLD}{name}{Colors.RESET}{mark}: {', '.join(spec)}")
        print(textwrap.indent(textwrap.fill(", ".join(selected), _term_width() - 4, break_on_hyphens=False), "    "))
