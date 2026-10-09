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

"""Control statements: keyword spacing and brace placement."""

import re
from typing import Optional

from mynewt_style import FileContext, Rule, register_rule
from mynewt_style.cparse import (Ctl, control_statements, is_do_while_tail as _is_do_while_tail,
                                 prev_nonspace as _prev_nonspace)

@register_rule
class KeywordSpace(Rule):
    name = "keyword-space"
    category = "control"
    summary = "One space after if/for/while/switch and around braces of control statements."
    explanation = """
        CODING_STANDARDS.md: "Put space after keywords (for, if, return,
        switch, while)."

        Good:  if (x) {        } else {        do {        } while (x);
        Bad:   if(x){          }else{          do{         }while (x);
    """
    fixable = True

    def _inserts(self, ctx: FileContext):
        code = ctx.code_nopp
        found = []
        for c in control_statements(code):
            if c.kw in ("if", "for", "while", "switch"):
                after_kw = c.kw_pos + len(c.kw)
                if code[after_kw] == "(":
                    found.append((after_kw, f"missing space after '{c.kw}'"))
            if c.body < len(code) and code[c.body] == "{" and c.body == c.head_end:
                found.append((c.head_end, f"missing space before '{{' after '{c.kw}'"))
            if c.kw in ("else", "while"):
                p = _prev_nonspace(code, c.kw_pos)
                if p >= 0 and code[p] == "}" and p == c.kw_pos - 1:
                    found.append((c.kw_pos, f"missing space between '}}' and '{c.kw}'"))
        return sorted(set(found))

    def check(self, ctx: FileContext):
        out = []
        for off, msg in self._inserts(ctx):
            ln, col = ctx.line_col(off)
            out.append(self.violation(ctx, ln, col + 1, msg))
        return out

    def fix(self, ctx: FileContext):
        ins = self._inserts(ctx)
        if not ins:
            return None
        c = ctx.content
        for off, _ in reversed(ins):
            c = c[:off] + " " + c[off:]
        return c


@register_rule
class ControlBracePlacement(Rule):
    name = "control-brace-placement"
    category = "control"
    summary = "Opening brace on the same line as if/for/while/switch/else/do; '} else' together."
    explanation = """
        CODING_STANDARDS.md: "Braces for statements must be on the same line
        as the statement" and no newline between '}' and 'else'.

        Good:                       Bad:
            if (x) {                    if (x)
                ...                     {
            } else {                        ...
                ...                     }
            }                           else {

        The same applies to the 'while' of a do/while loop ('} while (x);').
        Only joins across pure whitespace; if a comment sits in between, the
        violation is reported but left for you to resolve.
    """
    fixable = True

    def _joins(self, ctx: FileContext):
        """(start, end, message) spans of whitespace that should be one space."""
        code = ctx.code_nopp
        content = ctx.content
        out = []
        for c in control_statements(code):
            if c.body < len(code) and code[c.body] == "{" and "\n" in code[c.head_end:c.body]:
                out.append((c.head_end, c.body, f"'{{' must be on the same line as '{c.kw}'"))
            if c.kw == "else" or (c.kw == "while" and _is_do_while_tail(code, c.kw_pos)):
                p = _prev_nonspace(code, c.kw_pos)
                if p >= 0 and code[p] == "}" and "\n" in code[p:c.kw_pos]:
                    out.append((p + 1, c.kw_pos, f"'{c.kw}' must follow '}}' on the same line"))
        return [(s, e, msg, content[s:e].strip() == "") for s, e, msg in out]

    def check(self, ctx: FileContext):
        out = []
        for s, e, msg, ok in self._joins(ctx):
            ln, col = ctx.line_col(e)
            out.append(self.violation(ctx, ln, col + 1, msg, fixable=ok))
        return out

    def fix(self, ctx: FileContext):
        spans = [(s, e) for s, e, _, ok in self._joins(ctx) if ok]
        if not spans:
            return None
        c = ctx.content
        for s, e in sorted(spans, reverse=True):
            c = c[:s] + " " + c[e:]
        return c


@register_rule
class BracesRequired(Rule):
    name = "braces-required"
    category = "control"
    summary = "if/else/for/while/do bodies always use braces."
    explanation = """
        CODING_STANDARDS.md: "for, else, if, while statements must have braces
        around their code blocks".

        Good:                       Bad:
            if (x) {                    if (x)
                return 0;                   return 0;
            }

        The fix adds braces around a single simple statement (one that ends
        with ';' at the end of its line). Nested control statements without
        braces are reported but not fixed automatically.
    """
    fixable = True
    may_insert_tokens = ("{", "}")

    def _offenders(self, ctx: FileContext):
        code = ctx.code_nopp
        for c in control_statements(code):
            if c.kw == "switch" or c.body >= len(code):
                continue
            ch = code[c.body]
            if ch == "{":
                continue
            if c.kw == "else" and re.match(r"if\b", code[c.body:c.body + 3]):
                continue
            if c.kw == "while" and _is_do_while_tail(code, c.kw_pos):
                continue
            if ch == ";":
                continue  # empty loop body: 'while (busy());'
            yield c

    def _fix_plan(self, ctx: FileContext, c: Ctl) -> Optional[tuple]:
        """(statement_end_offset, eol_offset) if the body is a simple statement."""
        code = ctx.code_nopp
        if re.match(r"(if|for|while|switch|do|else|case|default)\b", code[c.body:]):
            return None
        depth = 0
        for i in range(c.body, len(code)):
            ch = code[i]
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
                if depth < 0:
                    return None
            elif ch == ";" and depth == 0:
                nl = code.find("\n", i)
                nl = len(code) if nl < 0 else nl
                if code[i + 1:nl].strip():
                    return None  # more code after the statement on that line
                first = ctx.line_col(c.head_end)[0]
                last = ctx.line_col(nl)[0]
                if any(ln in ctx.pp_lines for ln in range(first, last + 1)):
                    return None  # '#if' inside the body: braces could change meaning
                return i + 1, nl
        return None

    def check(self, ctx: FileContext):
        out = []
        for c in self._offenders(ctx):
            ln, col = ctx.line_col(c.kw_pos)
            out.append(self.violation(ctx, ln, col + 1, f"'{c.kw}' body must use braces",
                                      fixable=self._fix_plan(ctx, c) is not None))
        return out

    def fix(self, ctx: FileContext):
        content = ctx.content
        code = ctx.code_nopp
        edits = []  # (offset, text_to_insert, replace_len)
        for c in self._offenders(ctx):
            plan = self._fix_plan(ctx, c)
            if plan is None:
                continue
            _, eol = plan
            kw_line = ctx.line_col(c.kw_pos)[0]
            indent = re.match(r"[ \t]*", ctx.lines[kw_line - 1]).group(0)
            body_on_head_line = "\n" not in code[c.head_end:c.body]
            if body_on_head_line:
                step = " " * int(ctx.settings["indent_width"])
                # 'if (x) return;' -> brace, then body on its own line.
                edits.append((c.head_end, " {\n" + indent + step, c.body - c.head_end))
            else:
                edits.append((c.head_end, " {", 0))
            edits.append((eol, "\n" + indent + "}", 0))
        if not edits:
            return None
        for off, text, rep in sorted(edits, reverse=True):
            content = content[:off] + text + content[off + rep:]
        return content
