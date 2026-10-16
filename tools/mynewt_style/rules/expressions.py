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

"""Expression level style: operator spacing, sizeof, return, casts, literals."""

import re
from typing import Optional

from mynewt_style import FileContext, register_rule
from mynewt_style.cparse import C_KEYWORDS, find_matching, skip_ws
from mynewt_style.helpers import Edit, EditRule, is_blank, tokens

SPACED_OPERATORS = {
    "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=",
    "==", "!=", "<=", ">=", "&&", "||",
}


@register_rule
class OperatorSpacing(EditRule):
    name = "operator-spacing"
    category = "spacing"
    summary = "Spaces around assignment, comparison and logical operators."
    explanation = """
        Assignment ('=', '+=', ...), comparison ('==', '!=', '<=', '>=') and
        logical ('&&', '||') operators have a space on both sides:

            Good:  x = a + 1;      if (a == b && c != d)
            Bad:   x=a + 1;        if (a==b&&c!=d)

        Only missing spaces are added; extra spaces used to align a column of
        assignments are left alone. Arithmetic, bitwise and comparison '<'/'>'
        operators are not checked because '*', '&', '-' are also unary and
        '<'/'>' appear in #include lines.
    """
    fixable = True

    def edits(self, ctx: FileContext):
        code = ctx.code_macros
        for t in tokens(code):
            if t.text not in SPACED_OPERATORS:
                continue
            if t.start > 0 and not code[t.start - 1].isspace():
                yield Edit(t.start, t.start, " ", f"missing space before '{t.text}'")
            if t.end < len(code) and not code[t.end].isspace():
                yield Edit(t.end, t.end, " ", f"missing space after '{t.text}'")


@register_rule
class SpaceInsideParens(EditRule):
    name = "space-inside-parens"
    category = "spacing"
    summary = "No spaces just inside parentheses or brackets: '(x)', 'a[i]'."
    explanation = """
        Good:  foo(a, b)      buf[i]      if (x)
        Bad:   foo( a, b )    buf[ i ]    if ( x )

        Empty macro arguments ('FOO(a, )'), 'for (;;)' and line breaks right
        after '(' are fine. Spaces that hold a comment are not touched.
    """
    fixable = True

    def edits(self, ctx: FileContext):
        code = ctx.code_macros
        # Not before ',' (empty macro argument), ';' (for (;;)) or '\' (the
        # padding before a line continuation is alignment, not paren spacing).
        for m in re.finditer(r"[(\[]([ \t]+)(?=[^\s,;)\]\\])", code):
            if is_blank(ctx, m.start(1), m.end(1)):
                yield Edit(m.start(1), m.end(1), "", f"space after '{m.group(0)[0]}'")
        for m in re.finditer(r"(?<=[^\s,;(\[])([ \t]+)[)\]]", code):
            if is_blank(ctx, m.start(1), m.end(1)):
                yield Edit(m.start(1), m.end(1), "", f"space before '{m.group(0)[-1]}'")


@register_rule
class SpaceBeforeSemicolon(EditRule):
    name = "space-before-semicolon"
    category = "spacing"
    summary = "No space before ';'."
    explanation = """
        Good:  rc = foo();       for (i = 0; i < n; i++)
        Bad:   rc = foo() ;      for (i = 0 ; i < n ; i++)

        Empty for-clauses ('for (;;)', 'for (i = 0; ; i++)') are fine.
    """
    fixable = True

    def edits(self, ctx: FileContext):
        for m in re.finditer(r"(?<=[^\s;(])([ \t]+);", ctx.code_macros):
            if is_blank(ctx, m.start(1), m.end(1)):
                yield Edit(m.start(1), m.end(1), "", "space before ';'")


_CAST_RE = re.compile(
    r"\(\s*((?:(?:const|volatile|signed|unsigned|struct|union|enum)\s+)*"
    r"[A-Za-z_]\w*(?:\s+(?:int|long|short|char|const))*\s*\**)\s*\)([ \t]+)"
    r"(?=[\w(*&!~\-+\"'])")

# Words after which '(' starts an expression, so '(type) x' is a cast.
_CAST_CONTEXT = {"return", "case", "sizeof"}


@register_rule
class CastSpacing(EditRule):
    name = "cast-spacing"
    category = "spacing"
    summary = "No space after a cast: '(uint8_t)x'."
    explanation = """
        A cast binds tightly to its operand; a space makes it look like a
        separate expression.

            Good:  len = (uint16_t)om->om_len;     (void)ble_gap_terminate(h, r);
            Bad:   len = (uint16_t) om->om_len;    (void) ble_gap_terminate(h, r);

        Only parenthesised type names are recognised (C type keywords, names
        ending in '_t', 'struct/union/enum X', with optional '*').
    """
    fixable = True

    def edits(self, ctx: FileContext):
        code = ctx.code_nopp
        for m in _CAST_RE.finditer(code):
            words = re.findall(r"[A-Za-z_]\w*", m.group(1))
            last = words[-1]
            is_type = (last in C_KEYWORDS or last.endswith("_t")
                       or any(w in ("struct", "union", "enum") for w in words[:-1]))
            if not is_type or last in ("return", "sizeof", "case"):
                continue
            # What precedes '(' decides whether this is a cast at all:
            # 'foo(int) x' or 'sizeof(int) * n' are not casts.
            p = m.start() - 1
            while p >= 0 and code[p] in " \t":
                p -= 1
            if p >= 0 and (code[p].isalnum() or code[p] in "_)]"):
                word = re.search(r"(\w+)$", code[:p + 1])
                if not word or word.group(1) not in _CAST_CONTEXT or word.group(1) == "sizeof":
                    continue
            if is_blank(ctx, m.start(2), m.end(2)):
                yield Edit(m.start(2), m.end(2), "", f"space after cast '({m.group(1).strip()})'")


def _unary_operand_end(code: str, i: int) -> Optional[int]:
    """End of a unary-expression operand starting at i ('*p', 'x.y[2]', 'a->b'), or None."""
    while i < len(code) and code[i] in "*&":
        i = skip_ws(code, i + 1)
    m = re.compile(r"[A-Za-z_]\w*|\d\w*|\"[^\"\n]*\"").match(code, i)
    if not m:
        return None
    i = m.end()
    while True:
        j = skip_ws(code, i)
        if code.startswith("->", j) or (j < len(code) and code[j] == "."):
            k = skip_ws(code, j + (2 if code[j] == "-" else 1))
            m = re.compile(r"[A-Za-z_]\w*").match(code, k)
            if not m:
                return None
            i = m.end()
        elif j < len(code) and code[j] == "[":
            close = find_matching(code, j)
            if close is None:
                return None
            i = close + 1
        else:
            return i


@register_rule
class SizeofParens(EditRule):
    name = "sizeof-parens"
    category = "expressions"
    summary = "Always write sizeof(x), with parentheses and no space."
    explanation = """
        'sizeof x' is legal for expressions, but mixing it with sizeof(type)
        invites precedence mistakes ('sizeof x + 1', 'sizeof *p * n') and
        makes searches inconsistent. Always use the function-like form:

            Good:  memcpy(dst, src, sizeof(dst));     n = sizeof(*hdr);
            Bad:   memcpy(dst, src, sizeof dst);      n = sizeof *hdr;
                   n = sizeof (struct foo);

        The fix wraps simple operands (names, member accesses, array
        elements, dereferences); anything more complex is reported only.
    """
    fixable = True
    may_insert_tokens = ("(", ")")

    def edits(self, ctx: FileContext):
        code = ctx.code_macros
        for m in re.finditer(r"\bsizeof\b", code):
            q = skip_ws(code, m.end())
            if q >= len(code):
                continue
            if code[q] == "(":
                if q > m.end() and is_blank(ctx, m.end(), q):
                    yield Edit(m.end(), q, "", "no space between 'sizeof' and '('")
                continue
            end = _unary_operand_end(code, q)
            if end is None or "\n" in code[q:end] or not is_blank(ctx, m.end(), q):
                yield Edit(m.start(), m.end(), "", "use 'sizeof(...)' with parentheses",
                           fixable=False)
                continue
            yield Edit(m.end(), end, "(" + ctx.content[q:end] + ")",
                       f"use 'sizeof({ctx.content[q:end]})' with parentheses")


@register_rule
class ReturnParens(EditRule):
    name = "return-parens"
    category = "expressions"
    summary = "'return' is not a function: 'return x;', not 'return (x);'."
    explanation = """
        Good:  return rc;           return a + b;
        Bad:   return (rc);         return(a + b);

        Parentheses that are part of the expression ('return (a + b) * c;',
        casts, compound literals) are kept. A value split over several lines
        is reported but not rewritten.
    """
    fixable = True
    may_insert_tokens = ("(", ")")

    def edits(self, ctx: FileContext):
        code = ctx.code_nopp
        for m in re.finditer(r"\breturn([ \t]*)\(", code):
            open_ = m.end() - 1
            close = find_matching(code, open_)
            if close is None:
                continue
            after = skip_ws(code, close + 1)
            if after >= len(code) or code[after] != ";":
                continue
            inner = ctx.content[open_ + 1:close].strip()
            if not inner:
                continue
            fixable = "\n" not in code[open_:close]
            yield Edit(m.start() + len("return"), close + 1, " " + inner,
                       "unnecessary parentheses around return value", fixable)


def _inside_parens(code: str, idx: int) -> bool:
    """True if idx is inside an unclosed '(' of the current statement."""
    depth = 0
    i = idx - 1
    while i >= 0:
        c = code[i]
        if c in ")]":
            depth += 1
        elif c in "([":
            if depth == 0:
                return True
            depth -= 1
        elif c in "{}" and depth == 0:
            return False
        i -= 1
    return False


@register_rule
class StraySemicolon(EditRule):
    name = "stray-semicolon"
    category = "expressions"
    summary = "No empty statements: ';;' or ';' after a function or block body."
    explanation = """
        Good:  foo();                 }            (end of function or if-block)
        Bad:   foo();;                };

        Extra semicolons are harmless to the compiler but are noise, and a
        ';' after a function body at file scope is not valid ISO C.
        'for (;;)' is of course fine.
    """
    fixable = True
    may_insert_tokens = (";",)

    def edits(self, ctx: FileContext):
        code = ctx.code_nopp
        for m in re.finditer(r";(\s*);", code):
            second = m.end() - 1
            if _inside_parens(code, second) or not is_blank(ctx, m.start(1), m.end(1)):
                continue
            yield Edit(m.start() + 1, m.end(), "", "empty statement ';;'")
        for _sig, _open, close in ctx.function_bodies:
            nxt = skip_ws(code, close + 1)
            if nxt < len(code) and code[nxt] == ";" and is_blank(ctx, close + 1, nxt):
                yield Edit(close + 1, nxt + 1, "", "';' after function body")


@register_rule
class LiteralSuffix(EditRule):
    name = "literal-suffix"
    category = "expressions"
    summary = "Integer literal suffixes are upper case: 10U, 1UL, not 10u, 1ul."
    explanation = """
        MISRA C:2012 rule 7.3. A lower case 'l' is easily misread as the digit
        '1' ('10l' vs '101'); writing all suffixes upper case keeps them
        uniform.

            Good:  0xFFU    1UL << 31    100000L
            Bad:   0xFFu    1ul << 31    100000l
    """
    fixable = True

    def normalize_token(self, token: str) -> str:
        return token.upper() if token[:1].isdigit() else token

    def edits(self, ctx: FileContext):
        for m in re.finditer(r"\b(0[xX][0-9a-fA-F]+|\d+)([uUlL]+)\b", ctx.code_macros):
            suffix = m.group(2)
            if suffix != suffix.upper():
                yield Edit(m.start(2), m.end(2), suffix.upper(),
                           f"lower case literal suffix '{suffix}'; use '{suffix.upper()}'")
