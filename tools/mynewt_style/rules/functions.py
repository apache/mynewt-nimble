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

"""Function definition / declaration layout."""

import re
from typing import List, Optional

from mynewt_style import FileContext, Rule, register_rule
from mynewt_style.cparse import FuncSig, find_matching, split_top_level


def _eol(line: str) -> str:
    return "\r" if line.endswith("\r") else ""


def _pack_params(head: str, params: List[str], tail: str, max_len: int) -> Optional[List[str]]:
    """
    Bin-pack 'head(p1, p2, ...)tail' into lines no longer than max_len, with
    continuation lines aligned after '('. Falls back to a 4-space hanging
    indent ('head(' alone on the first line). Returns None if nothing fits.
    """
    pieces = [p + "," for p in params[:-1]] + [params[-1] + ")" + tail]

    def pack(first: str, indent: int) -> Optional[List[str]]:
        lines = [first]
        for piece in pieces:
            cur = lines[-1]
            sep = "" if cur.endswith("(") or cur.strip() == "" else " "
            if len(cur) + len(sep) + len(piece) <= max_len:
                lines[-1] = cur + sep + piece
            else:
                if indent + len(piece) > max_len:
                    return None
                lines.append(" " * indent + piece)
        return lines

    aligned = pack(head + "(", len(head) + 1)
    if aligned is not None:
        return aligned
    # 'head(' alone, all parameters on 4-space indented lines.
    hanging = pack(" " * 4, 4)
    if hanging is None:
        return None
    return [head + "("] + hanging


def _params(ctx: FileContext, sig: FuncSig) -> Optional[List[str]]:
    """Normalised parameter texts, or None if comments/#ifs make reflowing unsafe."""
    first = sig.name_line
    last = ctx.line_col(sig.close_paren)[0]
    if any(ln in ctx.comment_lines or ln in ctx.pp_lines for ln in range(first, last + 1)):
        return None
    inner = ctx.content[sig.open_paren + 1:sig.close_paren]
    masked = ctx.code[sig.open_paren + 1:sig.close_paren]
    params = [re.sub(r"\s+", " ", p).strip() for p in split_top_level(inner, masked)]
    if any(not p for p in params) and params != [""]:
        return None
    return params


@register_rule
class ReturnTypePlacement(Rule):
    name = "return-type-placement"
    category = "functions"
    summary = "Return type and function name are on the same line."
    explanation = """
        With style = "same-line" (default) a function definition or prototype
        is written with its return type on the name's line:

            static int ble_foo_init(uint8_t a, uint16_t b)
            {

        not:

            static int
            ble_foo_init(uint8_t a, uint16_t b)
            {

        The fix joins the lines and moves continuation lines that were
        aligned to the '(' along with it. If the signature then exceeds the
        line limit, the parameters are re-packed to fit.

        With style = "own-line" the opposite is enforced (the layout
        CODING_STANDARDS.md and clang-format currently use).

        Only file-scope declarations starting in column 0 are considered;
        function pointers and declarations with attributes or macros between
        type and name are left alone.
    """
    fixable = True
    options = {"style": "same-line"}

    def __init__(self, options=None, severity=None):
        super().__init__(options, severity)
        if self.opts["style"] not in ("same-line", "own-line"):
            from mynewt_style import RuleError
            raise RuleError(f"rule '{self.name}': style must be 'same-line' or 'own-line'")

    def check(self, ctx: FileContext):
        out = []
        same = self.opts["style"] == "same-line"
        for sig in ctx.functions:
            if same and sig.split:
                rtype = ctx.lines[sig.type_line - 1].strip()
                out.append(self.violation(
                    ctx, sig.type_line, 1,
                    f"return type '{rtype}' must be on the same line as '{sig.name}('"))
            elif not same and not sig.split:
                out.append(self.violation(
                    ctx, sig.name_line, 1,
                    f"return type of '{sig.name}(' must be on its own line"))
        return out

    def fix(self, ctx: FileContext):
        same = self.opts["style"] == "same-line"
        lines = ctx.content.split("\n")
        max_len = int(ctx.settings["max_line_length"])
        changed = False
        # Bottom-up so earlier line numbers stay valid.
        for sig in sorted(ctx.functions, key=lambda s: s.type_line, reverse=True):
            if same and sig.split:
                new = self._join(ctx, sig, max_len)
                if new is None:
                    continue
                lines[sig.type_line - 1:ctx.line_col(sig.close_paren)[0]] = new
                changed = True
            elif not same and not sig.split:
                new = self._split(ctx, sig)
                lines[sig.name_line - 1:ctx.line_col(sig.close_paren)[0]] = new
                changed = True
        return "\n".join(lines) if changed else None

    def _join(self, ctx: FileContext, sig: FuncSig, max_len: int) -> Optional[List[str]]:
        rtype = ctx.lines[sig.type_line - 1].rstrip()
        prefix = rtype + ("" if rtype.endswith("*") else " ")
        shift = len(prefix)
        paren_col = ctx.line_col(sig.open_paren)[1]
        last = ctx.line_col(sig.close_paren)[0]
        sig_lines = ctx.lines[sig.name_line - 1:last]
        new = [prefix + sig_lines[0]]
        for line in sig_lines[1:]:
            indent = len(line) - len(line.lstrip(" "))
            new.append(" " * shift + line if indent == paren_col + 1 else line)
        if all(len(l.rstrip("\r")) <= max_len for l in new):
            return new
        params = _params(ctx, sig)
        if params is None:
            return None
        eol = _eol(sig_lines[-1])
        close_col = ctx.line_col(sig.close_paren)[1]
        tail = sig_lines[-1][close_col + 1:len(sig_lines[-1]) - len(eol)].rstrip()
        head = prefix + sig_lines[0][:paren_col].rstrip()
        packed = _pack_params(head, params, tail, max_len)
        if packed is None:
            return None
        return [l + eol for l in packed]

    def _split(self, ctx: FileContext, sig: FuncSig) -> List[str]:
        line = ctx.lines[sig.name_line - 1]
        shift = sig.name_col
        paren_col = ctx.line_col(sig.open_paren)[1]
        last = ctx.line_col(sig.close_paren)[0]
        new = [line[:shift].rstrip() + _eol(line), line[shift:]]
        for cont in ctx.lines[sig.name_line:last]:
            indent = len(cont) - len(cont.lstrip(" "))
            new.append(cont[shift:] if indent == paren_col + 1 else cont)
        return new


@register_rule
class FunctionBraceNewline(Rule):
    name = "function-brace-newline"
    category = "functions"
    summary = "The opening brace of a function body goes on its own line."
    explanation = """
        CODING_STANDARDS.md: "After a function declaration, the braces should
        be on a newline".

        Good:                               Bad:
            static int ble_foo(void)            static int ble_foo(void) {
            {

        Short one-line inline functions ('static inline int f(void) { return 0; }')
        are allowed.
    """
    fixable = True

    def _offenders(self, ctx: FileContext):
        for sig in ctx.functions:
            if not sig.is_definition:
                continue
            brace_line, brace_col = ctx.line_col(sig.brace)
            close_line = ctx.line_col(sig.close_paren)[0]
            if brace_line != close_line:
                continue
            end = find_matching(ctx.code_nopp, sig.brace)
            if end is not None and ctx.line_col(end)[0] == brace_line:
                continue  # one-liner
            yield sig, brace_line, brace_col

    def check(self, ctx: FileContext):
        out = []
        for sig, ln, col in self._offenders(ctx):
            rest = ctx.code_lines[ln - 1][col + 1:].strip()
            out.append(self.violation(ctx, ln, col + 1,
                                      f"opening brace of '{sig.name}()' must be on its own line",
                                      fixable=not rest))
        return out

    def fix(self, ctx: FileContext):
        lines = ctx.content.split("\n")
        changed = False
        for sig, ln, col in sorted(self._offenders(ctx), key=lambda o: -o[1]):
            line = lines[ln - 1]
            if ctx.code_lines[ln - 1][col + 1:].strip():
                continue
            eol = _eol(line)
            lines[ln - 1:ln] = [line[:col].rstrip() + eol, line[col:]]
            changed = True
        return "\n".join(lines) if changed else None


_NOT_CALLS = {
    "if", "for", "while", "switch", "return", "sizeof", "case", "do", "else",
    "defined", "typeof", "__typeof__", "_Alignof", "alignof", "_Static_assert",
    "__attribute__", "__asm__", "__asm", "asm", "volatile", "__volatile__",
    "void", "char", "short", "int", "long", "float", "double", "signed",
    "unsigned", "const", "static", "inline", "extern", "struct", "union", "enum",
    "bool", "_Bool", "register", "goto",
}

_SPACE_PAREN = re.compile(r"\b([A-Za-z_]\w*)([ \t]+)\(")


@register_rule
class CallParenSpacing(Rule):
    name = "call-paren-spacing"
    category = "spacing"
    summary = "No space between a function name and '(' (calls and declarations)."
    explanation = """
        CODING_STANDARDS.md: "No spaces after function names when calling a
        function".

        Good:  rc = ble_foo(a, b);
        Bad:   rc = ble_foo (a, b);

        Keywords (if, while, sizeof, return, ...), type names followed by a
        function pointer declarator ('void (*cb)(void)') and macro bodies are
        not affected.
    """
    fixable = True

    def _matches(self, ctx: FileContext):
        code = ctx.code_nopp
        for m in _SPACE_PAREN.finditer(code):
            word = m.group(1)
            if word in _NOT_CALLS or word.startswith("__"):
                continue
            nxt = code[m.end():m.end() + 1]
            if nxt in ("*", "^"):
                continue
            yield m

    def check(self, ctx: FileContext):
        out = []
        for m in self._matches(ctx):
            ln, col = ctx.line_col(m.start(2))
            out.append(self.violation(ctx, ln, col + 1,
                                      f"space between '{m.group(1)}' and '('"))
        return out

    def fix(self, ctx: FileContext):
        spans = [(m.start(2), m.end(2)) for m in self._matches(ctx)]
        if not spans:
            return None
        c = ctx.content
        for s, e in reversed(spans):
            c = c[:s] + c[e:]
        return c
