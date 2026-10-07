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

"""Rules that catch bug-prone constructs rather than layout."""

import re
from typing import List, Optional, Tuple

from mynewt_style import FileContext, Rule, register_rule
from mynewt_style.cparse import control_statements, find_matching, is_do_while_tail
from mynewt_style.helpers import tokens


@register_rule
class BannedFunctions(Rule):
    name = "banned-functions"
    category = "safety"
    summary = "Unbounded string/memory functions (sprintf, strcpy, ...) are not allowed."
    explanation = """
        These functions cannot be told how big the destination is, and are
        the classic source of buffer overflows - exactly the kind of bug the
        Mynewt and NimBLE threat models care about in packet handling code.
        Git, curl and the Linux kernel ban them the same way.

            Bad:   sprintf(buf, "%d", val);    strcpy(name, src);
            Good:  snprintf(buf, sizeof(buf), "%d", val);

        The list and the suggested replacements are the 'functions' option.
    """
    options = {
        "functions": {
            "sprintf": "snprintf",
            "vsprintf": "vsnprintf",
            "strcpy": "strlcpy, or memcpy with an explicit length",
            "strcat": "strlcat or snprintf",
            "strncpy": "strlcpy (strncpy does not always NUL-terminate)",
            "strncat": "strlcat",
            "gets": "fgets",
            "strtok": "strtok_r",
            "atoi": "strtol with error checking",
            "alloca": "a fixed-size buffer",
        },
    }

    def check(self, ctx: FileContext):
        banned = self.opts["functions"]
        if not banned:
            return []
        pattern = re.compile(r"\b(" + "|".join(map(re.escape, banned)) + r")\s*\(")
        out = []
        for m in pattern.finditer(ctx.code_macros):
            ln, col = ctx.line_col(m.start())
            out.append(self.violation(ctx, ln, col + 1,
                                      f"'{m.group(1)}' is banned; use {banned[m.group(1)]}"))
        return out


@register_rule
class OctalLiteral(Rule):
    name = "octal-literal"
    category = "safety"
    summary = "No octal integer literals (010 is eight, not ten)."
    explanation = """
        MISRA C:2012 rule 7.1, CERT DCL18-C. A leading zero silently turns a
        number into octal, which is almost always a mistake in protocol
        constants and tables.

            Bad:   uint8_t mask = 010;
            Good:  uint8_t mask = 8;    uint8_t mask = 0x08;
    """

    def check(self, ctx: FileContext):
        out = []
        for m in re.finditer(r"(?<![\w.])0([0-7]+)\b(?![.eE])", ctx.code_macros):
            if set(m.group(1)) == {"0"}:
                continue
            ln, col = ctx.line_col(m.start())
            out.append(self.violation(ctx, ln, col + 1,
                                      f"octal literal '{m.group(0)}' (= {int(m.group(0), 8)})"))
        return out


@register_rule
class BoolCompare(Rule):
    name = "bool-compare"
    category = "safety"
    summary = "Do not compare against true/false; test the value directly."
    explanation = """
        Comparing with 'true' is wrong for any non-bool that is "true" but not
        1, and both forms are redundant for real bools.

            Bad:   if (enabled == true)    if (is_set(x) != false)
            Good:  if (enabled)            if (is_set(x))
    """

    def check(self, ctx: FileContext):
        out = []
        pat = r"(==|!=)\s*(true|false)\b|\b(true|false)\s*(==|!=)"
        for m in re.finditer(pat, ctx.code_nopp):
            ln, col = ctx.line_col(m.start())
            out.append(self.violation(ctx, ln, col + 1, "comparison with true/false"))
        return out


@register_rule
class AssignInCondition(Rule):
    name = "assign-in-condition"
    category = "safety"
    summary = "No assignments inside if/while conditions."
    explanation = """
        MISRA C:2012 rule 13.4. 'if (x = y)' is usually a typo for '==', and
        even when intended it hides a side effect in a test.

            Bad:   if ((rc = ble_foo()) != 0)      while ((om = next()))
            Good:  rc = ble_foo();
                   if (rc != 0) {
    """

    def check(self, ctx: FileContext):
        code = ctx.code_nopp
        out = []
        for c in control_statements(code):
            if c.kw not in ("if", "while"):
                continue
            open_ = code.index("(", c.kw_pos)
            for t in tokens(code, open_ + 1, c.head_end - 1):
                if t.text == "=":
                    ln, col = ctx.line_col(t.start)
                    out.append(self.violation(ctx, ln, col + 1,
                                              f"assignment inside '{c.kw}' condition"))
        return out


# -- switch statements -------------------------------------------------------

def _switch_labels(code: str, open_: int, close: int) -> List[Tuple[int, int]]:
    """(label_start, after_colon) of case/default labels directly in a switch body."""
    labels = []
    depth = 0
    i = open_ + 1
    while i < close:
        c = code[i]
        if c in "({[":
            depth += 1
        elif c in ")}]":
            depth -= 1
        elif depth == 0 and (c == "c" or c == "d"):
            m = re.compile(r"\b(case\b|default\s*:)").match(code, i)
            if m and (i == 0 or not (code[i - 1].isalnum() or code[i - 1] == "_")):
                j = i + (len("case") if m.group(1) == "case" else 0)
                pdepth = 0
                while j < close:
                    if code[j] in "([":
                        pdepth += 1
                    elif code[j] in ")]":
                        pdepth -= 1
                    elif code[j] == ":" and pdepth == 0:
                        break
                    j += 1
                labels.append((i, j + 1))
                i = j + 1
                continue
        i += 1
    return labels


def _match_open(text: str, close: int) -> Optional[int]:
    """Index of the bracket opening text[close] (scanning backwards)."""
    closer = text[close]
    opener = {"}": "{", ")": "(", "]": "["}[closer]
    depth = 0
    for i in range(close, -1, -1):
        if text[i] == closer:
            depth += 1
        elif text[i] == opener:
            depth -= 1
            if depth == 0:
                return i
    return None


def _ends_word(text: str, word: str) -> bool:
    return bool(re.search(r"(?<![\w])" + word + r"$", text))


class _Flow:
    def __init__(self, noreturn: List[str]):
        names = "|".join(map(re.escape, noreturn)) or r"(?!x)x"
        self.terminator = re.compile(r"(break|continue|return|goto)\b|(" + names + r")\s*\(")

    def terminates(self, text: str) -> bool:
        """True if code text (masked) cannot fall through its end."""
        t = text.strip()
        if not t:
            return False
        if t.endswith(";"):
            depth = 0
            i = len(t) - 2
            while i >= 0:
                c = t[i]
                if c in ")]":
                    depth += 1
                elif c in "([":
                    depth -= 1
                elif depth == 0 and c in ";{}":
                    break
                i -= 1
            last = t[i + 1:-1].strip()
            last = re.sub(r"^(\w+\s*:)+\s*", "", last)  # labels before it
            return bool(self.terminator.match(last))
        if t.endswith("}"):
            return self._block_terminates(t)
        return False

    def _block_terminates(self, t: str) -> bool:
        o = _match_open(t, len(t) - 1)
        if o is None or not self.terminates(t[o + 1:-1]):
            return False
        before = t[:o].rstrip()
        if _ends_word(before, "else"):
            # Every branch of the if / else if chain must terminate.
            t = before[:-4].rstrip()
            while True:
                if not t.endswith("}"):
                    return False
                o = _match_open(t, len(t) - 1)
                if o is None or not self.terminates(t[o + 1:-1]):
                    return False
                head = t[:o].rstrip()
                if not head.endswith(")"):
                    return False
                po = _match_open(head, len(head) - 1)
                kw = head[:po].rstrip() if po is not None else ""
                if not _ends_word(kw, "if"):
                    return False
                kw = kw[:-2].rstrip()
                if _ends_word(kw, "else"):
                    t = kw[:-4].rstrip()
                    continue
                return True
        if before.endswith(")") or _ends_word(before, "do"):
            return False  # if without else, loop or switch: may fall through
        return True  # plain { } block


_FALLTHROUGH_RE = re.compile(
    r"fall(s|ing)?[\s\-]*thr(ough|u)|__fallthrough|\bfallthrough\b|no\s+break", re.I)


def _switches(code: str):
    for c in control_statements(code):
        if c.kw == "switch" and c.body < len(code) and code[c.body] == "{":
            close = find_matching(code, c.body)
            if close is not None:
                yield c, c.body, close


@register_rule
class SwitchDefault(Rule):
    name = "switch-default"
    category = "safety"
    summary = "Every switch has a 'default:' label."
    explanation = """
        MISRA C:2012 rule 16.4. An explicit default documents what happens to
        unexpected values - important when switching on opcodes or fields
        received over the air.

            switch (op) {
            case BLE_ATT_OP_READ_REQ:
                ...
                break;
            default:
                return BLE_HS_ENOTSUP;    /* or just 'break;' if nothing to do */
            }
    """

    def check(self, ctx: FileContext):
        code = ctx.code_nopp
        out = []
        for c, open_, close in _switches(code):
            labels = _switch_labels(code, open_, close)
            if not any(code.startswith("default", s) for s, _ in labels):
                ln, col = ctx.line_col(c.kw_pos)
                out.append(self.violation(ctx, ln, col + 1, "switch without 'default:'"))
        return out


@register_rule
class ImplicitFallthrough(Rule):
    name = "implicit-fallthrough"
    category = "safety"
    summary = "A case that falls into the next one must say so with a comment."
    explanation = """
        A missing 'break' is one of the most common C bugs. Falling through on
        purpose is fine, but must be marked so readers (and GCC's
        -Wimplicit-fallthrough) know it is intended:

            case A:
                prepare();
                /* fall through */
            case B:
                run();
                break;

        '/* no break */' and '__fallthrough' are accepted as markers too.
        Empty labels stacked on top of each other ('case A: case B:') are not
        fall-through. A case is considered terminated by break, return, goto,
        continue, a call listed in 'noreturn_calls', or an if/else whose every
        branch terminates.
    """
    options = {"noreturn_calls": ["abort", "exit", "_exit", "__builtin_unreachable"]}

    def check(self, ctx: FileContext):
        code = ctx.code_nopp
        flow = _Flow(self.opts["noreturn_calls"])
        out = []
        for _c, open_, close in _switches(code):
            labels = _switch_labels(code, open_, close)
            for (_s1, body_start), (s2, _e2) in zip(labels, labels[1:]):
                body = code[body_start:s2]
                if not body.strip() or flow.terminates(body):
                    continue
                if _FALLTHROUGH_RE.search(ctx.content[body_start:s2]):
                    continue
                ln, col = ctx.line_col(s2)
                out.append(self.violation(ctx, ln, col + 1,
                                          "previous case falls through; add 'break;' or a "
                                          "'/* fall through */' comment"))
        return out


@register_rule
class EmptyBody(Rule):
    name = "empty-body"
    category = "safety"
    summary = "No empty statement as the body of if/else/for/while."
    explanation = """
        'if (x);' is almost always a stray semicolon that makes the next
        block run unconditionally. Busy-wait loops must use braces to show
        the empty body is intended.

            Bad:   if (rc != 0);          while (!ready());
            Good:  if (rc != 0) {         while (!ready()) {
                       ...                }
                   }
    """

    def check(self, ctx: FileContext):
        code = ctx.code_nopp
        out = []
        for c in control_statements(code):
            if c.kw in ("do", "switch") or c.body >= len(code) or code[c.body] != ";":
                continue
            if c.kw == "while" and is_do_while_tail(code, c.kw_pos):
                continue
            ln, col = ctx.line_col(c.body)
            what = "stray ';' after" if c.kw in ("if", "else") else "empty body of"
            out.append(self.violation(ctx, ln, col + 1, f"{what} '{c.kw}'"))
        return out
