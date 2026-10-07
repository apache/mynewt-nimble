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

"""Preprocessor directives and macros."""

import re
from collections import Counter
from dataclasses import dataclass
from typing import List, Optional

from mynewt_style import FileContext, Rule, register_rule
from mynewt_style.cparse import split_top_level

_TAKES_ARG = {"define", "undef", "include", "if", "ifdef", "ifndef", "elif",
              "error", "warning", "pragma", "line", "include_next"}

_DIRECTIVE_RE = re.compile(r"^([ \t]*)#([ \t]*)(\w+)([ \t]*)(.*)$")


def _strip_eol(line: str):
    return (line[:-1], "\r") if line.endswith("\r") else (line, "")


@register_rule
class DirectiveFormat(Rule):
    name = "directive-format"
    category = "preprocessor"
    summary = "'#' in column 0, no space after '#', one space before the argument."
    explanation = """
        Preprocessor directives start in column 0 and are not indented, even
        inside functions or nested #if blocks:

            Good:  #if MYNEWT_VAL(BLE_ROLE_CENTRAL)      #include "host/ble_hs.h"
            Bad:       #if MYNEWT_VAL(BLE_ROLE_CENTRAL)  #  include  "host/ble_hs.h"

        The spacing *after* a '#define NAME' (the value column) is handled by
        'define-alignment' and is not touched here.
    """
    fixable = True
    options = {"allow_space_after_hash": False}

    def _problems(self, ctx: FileContext):
        for d in ctx.directives:
            raw, eol = _strip_eol(ctx.lines[d.start_line - 1])
            m = _DIRECTIVE_RE.match(raw)
            if not m:
                continue
            lead, after_hash, name, gap, arg = m.groups()
            fixed = "#"
            msgs = []
            if lead:
                msgs.append((1, "preprocessor directive must start in column 1"))
            if after_hash and not self.opts["allow_space_after_hash"]:
                msgs.append((len(lead) + 2, "no space allowed between '#' and the directive"))
                fixed += name
            else:
                fixed += after_hash + name
            if name in _TAKES_ARG and arg:
                # '#if(x)' is unusual but harmless; '#include<x>' is not.
                want = "" if gap == "" and name != "include" else " "
                if gap != want:
                    col = len(lead) + 1 + len(after_hash) + len(name) + 1
                    msgs.append((col, f"use exactly one space after '#{name}'"))
                fixed += want + arg
            else:
                # '#endif  /* FOO */' etc: spacing before a comment is free.
                fixed += gap + arg
            if msgs:
                yield d.start_line, msgs, fixed + eol

    def check(self, ctx: FileContext):
        return [self.violation(ctx, ln, col, msg)
                for ln, msgs, _ in self._problems(ctx) for col, msg in msgs]

    def fix(self, ctx: FileContext):
        lines = ctx.content.split("\n")
        changed = False
        for ln, _, fixed in self._problems(ctx):
            if lines[ln - 1] != fixed:
                lines[ln - 1] = fixed
                changed = True
        return "\n".join(lines) if changed else None


_DEFINE_RE = re.compile(r"^#define[ \t]+(\w+(?:\([^)]*\))?)([ \t]+)(\S.*)$")


@dataclass
class _Def:
    line: int          # 1-based
    name_end: int      # visual column just after NAME / NAME(args)
    gap: int
    value_col: int     # visual column of the value
    has_tab: bool
    multiline: bool    # value continues on following lines
    value_len: int = 0 # length of the value text (for the line limit)


@register_rule
class DefineAlignment(Rule):
    name = "define-alignment"
    category = "preprocessor"
    summary = "#define values: one space, or aligned in a column with neighbouring #defines."
    explanation = """
        A #define is separated from its value by a single space, *unless* the
        author lined the values up in a column. Lined-up groups are tidy and
        are left exactly as they are:

            #define BLE_ATT_OP_ERROR_RSP        0x01     <- fine (aligned group)
            #define BLE_ATT_OP_MTU_REQ          0x02
            #define BLE_ATT_OP_MTU_RSP          0x03

            #define BLE_FOO 1                            <- fine (single space)

            #define BLE_BAR      1                       <- violation: extra spaces
                                                            that align with nothing

        #defines on consecutive lines form a block. If any value in a block
        is padded with extra spaces, the block is meant to be lined up, so
        all its values must start in the same column: the one most of them
        use (the rightmost on a tie), or further right if the longest name
        in the block needs it - the longest name sets the column and the
        rest follow. (Only if that would push a line over the line limit do
        over-long names stick out with one space instead.) This catches
        "aligned" blocks where every name simply got the same number of
        spaces:

            #define BTP_SERVICE_ID_CORE    0             <- violation: values in
            #define BTP_SERVICE_ID_GAP    1                 three different columns
            #define BTP_SERVICE_ID_L2CAP    3

        A lone padded #define (blank or comment lines around it) is fine if
        another #define in the file has its value in the same column - so a
        file-wide column (e.g. all HCI opcodes at column 57, each above its
        own struct) is respected. Otherwise its value moves to the column
        its neighbours use, or to a single space.
    """
    fixable = True

    def _groups(self, ctx: FileContext) -> List[List[_Def]]:
        tw = int(ctx.settings["tab_width"])
        self._ends = []
        groups: List[List[_Def]] = []
        cur: List[_Def] = []
        last_end = None
        blank_or_comment = set()
        for ln, (raw, code) in enumerate(zip(ctx.lines, ctx.code_lines), 1):
            if not code.strip() and (not raw.strip() or ln in ctx.comment_lines):
                blank_or_comment.add(ln)
        for d in ctx.directives:
            if d.name != "define":
                continue
            raw, _ = _strip_eol(ctx.lines[d.start_line - 1])
            m = _DEFINE_RE.match(raw)
            if not m or m.group(3).strip() == "\\":
                continue
            # Visual columns, so tab-aligned values compare correctly.
            name_end = len(raw[:m.end(1)].expandtabs(tw))
            value_col = len(raw[:m.start(3)].expandtabs(tw))
            entry = _Def(d.start_line, name_end, value_col - name_end, value_col, "\t" in raw,
                         d.end_line > d.start_line, len(m.group(3).expandtabs(tw)))
            self._ends.append((entry, d.end_line))
            contiguous = last_end is not None and all(
                ln in blank_or_comment for ln in range(last_end + 1, d.start_line))
            if not contiguous and cur:
                groups.append(cur)
                cur = []
            cur.append(entry)
            last_end = d.end_line
        if cur:
            groups.append(cur)
        return groups

    @staticmethod
    def _blocks(groups: List[List[_Def]], ends: dict) -> List[List[_Def]]:
        """Runs of #defines on consecutive lines (no blank or comment line)."""
        blocks: List[List[_Def]] = []
        for group in groups:
            for d in group:
                if blocks and blocks[-1] and ends[id(blocks[-1][-1])] + 1 == d.line:
                    blocks[-1].append(d)
                else:
                    blocks.append([d])
        return blocks

    def _plan(self, ctx: FileContext) -> List[tuple]:
        """(define, neighbour group, target column or None, message) to fix."""
        max_len = int(ctx.settings["max_line_length"])
        groups = self._groups(ctx)
        ends = {id(d): end for d, end in self._ends}
        file_cols = Counter(d.value_col for g in groups for d in g)
        group_of = {id(d): g for g in groups for d in g}
        plan: List[tuple] = []
        lone: List[_Def] = []
        for block in self._blocks(groups, ends):
            padded = [d for d in block if d.gap > 1]
            if len(block) >= 2 and padded:
                # The block is meant to be lined up: one column for all
                # values - the one most of them use (rightmost on a tie),
                # moved right if needed so the longest name still fits.
                counts = Counter(d.value_col for d in padded)
                best = max(counts.items(), key=lambda kv: (kv[1], kv[0]))[0]
                movable = [d for d in block if not d.multiline]
                widest = max((d.name_end + 1 for d in movable), default=best)
                longest_value = max((d.value_len for d in movable), default=0)
                # Never push a line over the hard limit just to align it;
                # then over-long names stick out with one space instead.
                if max(best, widest) + longest_value <= max_len:
                    best = max(best, widest)
                for d in block:
                    if d.multiline:
                        continue  # continuation lines hang off it; never moved
                    if d.name_end + 1 > best:
                        if d.gap > 1:  # too long for the column: one space
                            plan.append((d, None, d.name_end + 1,
                                         "name too long for the aligned column; "
                                         "use one space"))
                    elif d.value_col != best:
                        plan.append((d, None, best,
                                     f"value not aligned with the other #defines of its "
                                     f"block (column {best + 1})"))
            elif padded and not block[0].multiline and file_cols[block[0].value_col] < 2:
                lone.append(block[0])
        # A single padded #define is fine if another #define in the file uses
        # the same column (file-wide columns, e.g. one opcode per struct).
        stray = {id(d) for d in lone}
        for d in lone:
            plan.append((d, group_of[id(d)], None,
                         f"{d.gap} spaces before the #define value align with no other "
                         f"#define; use one space or line it up with its neighbours"))
        plan.sort(key=lambda p: p[0].line)
        self._stray_ids = stray
        return plan

    def check(self, ctx: FileContext):
        return [self.violation(ctx, d.line, d.name_end + 1, msg)
                for d, _g, _t, msg in self._plan(ctx)]

    @staticmethod
    def _fix_column(d: _Def, group: List[_Def], stray: set) -> int:
        """Where the value of a lone stray define d should go."""
        others = Counter(o.value_col for o in group
                         if o is not d and o.gap > 1 and o.value_col > d.name_end)
        # A column shared by at least two neighbours is real alignment.
        shared = [c for c, n in others.most_common() if n >= 2]
        if shared:
            return shared[0]
        # Several padded defines that just miss each other: the author meant
        # to align them, so line them up on the widest of their columns.
        near = [o.value_col for o in group
                if o is not d and id(o) in stray and o.value_col > d.name_end]
        if near:
            return max(near + [d.value_col])
        return d.name_end + 1

    def fix(self, ctx: FileContext):
        lines = ctx.content.split("\n")
        changed = False
        for d, group, target, _msg in self._plan(ctx):
            if d.has_tab:
                continue  # tabs: let no-tabs normalise the line first
            if target is None:
                target = self._fix_column(d, group, self._stray_ids)
            raw = lines[d.line - 1]
            new = raw[:d.name_end] + " " * (target - d.name_end) + raw[d.value_col:]
            if new != raw:
                lines[d.line - 1] = new
                changed = True
            d.gap, d.value_col = target - d.name_end, target
        return "\n".join(lines) if changed else None


@register_rule
class MacroContinuation(Rule):
    name = "macro-continuation"
    category = "preprocessor"
    summary = "Line-continuation backslashes of a macro are aligned (or all one space away)."
    explanation = """
        In a multi-line macro every trailing '\\' sits in the same column, so
        the macro reads as one block:

            #define BLE_FOO(x)                \\
                do {                          \\
                    ble_foo_do(x);            \\
                } while (0)

        Using exactly one space before every '\\' is accepted too. A mix of
        both, or a '\\' glued to the code, is reported. The fix aligns all
        backslashes to the most used existing column that fits every line, or
        just past the longest line.
    """
    fixable = True

    def _macros(self, ctx: FileContext):
        tw = int(ctx.settings["tab_width"])
        for d in ctx.directives:
            if d.end_line == d.start_line:
                continue
            rows = []
            for ln in range(d.start_line, d.end_line):
                raw, _ = _strip_eol(ctx.lines[ln - 1])
                if not raw.endswith("\\"):
                    break
                content = raw[:-1].rstrip(" \t").expandtabs(tw)
                rows.append((ln, content, len(raw[:-1].expandtabs(tw))))
            if rows:
                yield d, rows

    @staticmethod
    def _target(rows) -> int:
        """Most used existing column that fits every line, else just past the longest."""
        longest = max(len(content) for _, content, _ in rows)
        fitting = Counter(col for _, _, col in rows if col > longest)
        return fitting.most_common(1)[0][0] if fitting else longest + 1

    @staticmethod
    def _ok(rows) -> bool:
        cols = {col for _, _, col in rows}
        gaps = {col - len(content) for _, content, col in rows}
        if 0 in gaps:
            return False
        return len(cols) == 1 or gaps == {1}

    def check(self, ctx: FileContext):
        out = []
        for d, rows in self._macros(ctx):
            if self._ok(rows):
                continue
            target = self._target(rows)
            bad = next(r for r in rows if r[2] != target)
            out.append(self.violation(ctx, bad[0], bad[2] + 1,
                                      "line-continuation '\\' not aligned with the rest "
                                      "of the macro"))
        return out

    def fix(self, ctx: FileContext):
        lines = ctx.content.split("\n")
        changed = False
        for d, rows in self._macros(ctx):
            if self._ok(rows):
                continue
            if any("\t" in ctx.lines[ln - 1] for ln, _, _ in rows):
                continue  # let no-tabs normalise the macro first
            target = self._target(rows)
            for ln, content, _ in rows:
                _, eol = _strip_eol(lines[ln - 1])
                lines[ln - 1] = content.ljust(target) + "\\" + eol
            changed = True
        return "\n".join(lines) if changed else None


@register_rule
class MacroMultiStatement(Rule):
    name = "macro-multi-statement"
    category = "preprocessor"
    summary = "Function-like macros with several statements are wrapped in do { } while (0)."
    explanation = """
        A macro that expands to several statements breaks when used as the
        body of an unbraced if/else. Wrap it so it behaves like one statement:

            Good:  #define RESET(x)  do { (x)->a = 0; (x)->b = 0; } while (0)
            Bad:   #define RESET(x)  (x)->a = 0; (x)->b = 0

        Macros that expand to declarations or definitions (starting with a
        type, 'static', 'struct', ...) are not checked. Not auto-fixable.
    """
    severity = "warning"

    def check(self, ctx: FileContext):
        out = []
        for d in ctx.directives:
            if d.name != "define":
                continue
            text = "\n".join(ctx.code_lines[d.start_line - 1:d.end_line])
            m = re.match(r"\s*#\s*define\s+\w+\(([^)]*)\)", text)
            if not m:
                continue
            body = text[m.end():].replace("\\\n", " ").strip()
            if not body or re.match(r"(do\b|\(\{|static\b|struct\b|union\b|enum\b|"
                                    r"const\b|extern\b|typedef\b|void\b|int\b|\w+_t\b)", body):
                continue
            stmts = [s for s in split_top_level(body, sep=";") if s.strip()]
            if len(stmts) > 1:
                out.append(self.violation(ctx, d.start_line, 1,
                                          "multi-statement macro should be wrapped in "
                                          "'do { ... } while (0)'"))
        return out
