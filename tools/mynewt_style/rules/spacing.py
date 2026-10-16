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

"""Spacing inside statements: commas, pointers, wrapped operators."""

import re

from mynewt_style import FileContext, Rule, register_rule
from mynewt_style.cparse import C_KEYWORDS

@register_rule
class CommaSpace(Rule):
    name = "comma-space"
    category = "spacing"
    summary = "One space after a comma and none before it."
    explanation = """
        CODING_STANDARDS.md: "Arguments to function calls should have spaces
        between the comma".

        Good:  rc = foo(a, b, c);
        Bad:   rc = foo(a,b , c);

        Applies to code and macro bodies. Empty macro arguments ('FOO(a,,b)'),
        trailing commas and commas at the end of a line are fine.
    """
    fixable = True

    def _edits(self, ctx: FileContext):
        code = ctx.code_macros
        content = ctx.content
        out = []  # (start, end, replacement, message)
        for m in re.finditer(r"(?<!,),(?=[^\s,)\]}\\])", code):
            out.append((m.end(), m.end(), " ", "missing space after ','"))
        for m in re.finditer(r"(?<=[^\s,(\[])[ \t]+,", code):
            if content[m.start():m.end() - 1].strip() == "":
                out.append((m.start(), m.end() - 1, "", "space before ','"))
        return out

    def check(self, ctx: FileContext):
        out = []
        for s, _, _, msg in self._edits(ctx):
            ln, col = ctx.line_col(s)
            out.append(self.violation(ctx, ln, col + 1, msg))
        return out

    def fix(self, ctx: FileContext):
        edits = self._edits(ctx)
        if not edits:
            return None
        c = ctx.content
        for s, e, rep, _ in sorted(edits, reverse=True):
            c = c[:s] + rep + c[e:]
        return c


_TYPE_PREFIX = re.compile(r"\b(?:struct|union|enum)\s+$")


def _is_type_name(code: str, start: int, word: str) -> bool:
    if word in C_KEYWORDS:
        return word not in ("return", "sizeof", "case", "goto", "else", "do")
    if word.endswith("_t"):
        return True
    return bool(_TYPE_PREFIX.search(code[max(0, start - 10):start]))


@register_rule
class PointerAlignment(Rule):
    name = "pointer-alignment"
    category = "spacing"
    summary = "The '*' of a pointer declaration sticks to the name: 'char *p'."
    explanation = """
        The '*' binds to the declarator, not the type ('char *a, b' declares
        one pointer and one char), so it is written next to the name:

            Good:  struct os_mbuf *om;      (uint8_t *)buf
            Bad:   struct os_mbuf* om;      (uint8_t*)buf     uint8_t * buf

        Only recognised type names are considered (C keywords, names ending in
        '_t', and 'struct/union/enum X') so multiplications are never touched.
    """
    fixable = True

    def _edits(self, ctx: FileContext):
        code = ctx.code_nopp
        out = []
        # 'type* name' / 'type * name'
        for m in re.finditer(r"\b([A-Za-z_]\w*)([ \t]*)(\*+)([ \t]+)(?=[A-Za-z_(])", code):
            if not _is_type_name(code, m.start(1), m.group(1)):
                continue
            if m.group(2) == "":
                out.append((m.start(3), m.end(4), " " + m.group(3),
                            f"'{m.group(1)}{m.group(3)} ': put '*' next to the name"))
            else:
                out.append((m.start(4), m.end(4), "",
                            f"space after '*' in pointer declaration"))
        # cast: '(type*)'
        for m in re.finditer(r"\b([A-Za-z_]\w*)(\*+)\)", code):
            if _is_type_name(code, m.start(1), m.group(1)):
                out.append((m.start(2), m.start(2), " ",
                            f"missing space before '*' in '({m.group(1)}{m.group(2)})'"))
        return out

    def check(self, ctx: FileContext):
        out = []
        for s, _, _, msg in self._edits(ctx):
            ln, col = ctx.line_col(s)
            out.append(self.violation(ctx, ln, col + 1, msg))
        return out

    def fix(self, ctx: FileContext):
        edits = self._edits(ctx)
        if not edits:
            return None
        c = ctx.content
        for s, e, rep, _ in sorted(edits, reverse=True):
            c = c[:s] + rep + c[e:]
        return c


@register_rule
class OperatorAtLineEnd(Rule):
    name = "operator-at-line-end"
    category = "layout"
    summary = "When wrapping a condition, '&&' / '||' end the line instead of starting the next."
    explanation = """
        CODING_STANDARDS.md: "When you have to wrap a long statement, put the
        operator at the end of the line."

        Good:                        Bad:
            if (x &&                     if (x
                y == 10) {                   && y == 10) {

        The fix moves the operator to the end of the previous line; the
        operand keeps its column. Not fixed if the previous line ends in a
        comment or is a preprocessor directive.
    """
    fixable = True
    options = {"operators": ["&&", "||"]}

    def _offenders(self, ctx: FileContext):
        ops = sorted(self.opts["operators"], key=len, reverse=True)
        lines = ctx.code_lines
        for idx, code in enumerate(lines):
            if idx == 0 or (idx + 1) in ctx.pp_lines:
                continue
            stripped = code.lstrip()
            for op in ops:
                if stripped.startswith(op) and not stripped.startswith(op + "="):
                    prev = idx - 1
                    while prev >= 0 and not lines[prev].strip():
                        prev -= 1
                    if prev < 0:
                        break
                    fixable = (prev == idx - 1 and (prev + 1) not in ctx.comment_lines
                               and (prev + 1) not in ctx.pp_lines)
                    yield idx + 1, len(code) - len(stripped), op, fixable
                    break

    def check(self, ctx: FileContext):
        return [self.violation(ctx, ln, col + 1, f"line starts with '{op}'; "
                               f"put it at the end of the previous line", fixable=fx)
                for ln, col, op, fx in self._offenders(ctx)]

    def fix(self, ctx: FileContext):
        lines = ctx.content.split("\n")
        changed = False
        for ln, col, op, fixable in self._offenders(ctx):
            if not fixable:
                continue
            prev = lines[ln - 2]
            eol = "\r" if prev.endswith("\r") else ""
            lines[ln - 2] = prev[:len(prev) - len(eol)].rstrip() + " " + op + eol
            cur = lines[ln - 1]
            rest = cur[col + len(op):].lstrip(" \t")
            # An operator alone on its line leaves nothing behind.
            lines[ln - 1] = cur[:col] + rest if rest.strip() else None
            changed = True
        return "\n".join(l for l in lines if l is not None) if changed else None
