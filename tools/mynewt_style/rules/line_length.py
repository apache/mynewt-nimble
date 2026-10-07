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

"""Line length."""

import re

from mynewt_style import FileContext, Rule, register_rule
from mynewt_style.cparse import find_matching
from mynewt_style.wrap import wrap_line, wrap_lines


@register_rule
class LineLength(Rule):
    name = "line-length"
    category = "layout"
    summary = "Lines over 80 columns are warnings, over 100 are errors."
    explanation = """
        Keep lines at or below settings.target_line_length (80) where it reads
        well; going up to settings.max_line_length (100) is fine when breaking
        the line would hurt readability. Longer lines are errors.

        Lines matching one of 'ignore_patterns' (URLs, #include lines by
        default) are exempt because they cannot be broken sensibly.

        The fix wraps over-long code lines after the last ',' / '&&' / '||'
        that fits, aligning the rest the way the code base already does:

            rc = ble_gattc_write_flat(conn_handle, attr_handle, value, len,
                                      ble_gattc_write_cb, NULL);

        Comments, string literals and macros are never rewrapped; if no safe
        break point exists the line is left for you to shorten.

        Lines between target and max are warnings: shown, but they only fail
        the run with -W. Set 'report_target = false' to hide them.
    """
    fixable = True
    options = {
        "report_target": True,
        "ignore_patterns": [r"https?://", r"^\s*#\s*include\b"],
    }

    def _ignored(self, line: str) -> bool:
        return any(re.search(p, line) for p in self.opts["ignore_patterns"])

    def check(self, ctx: FileContext):
        hard = int(ctx.settings["max_line_length"])
        soft = int(ctx.settings["target_line_length"])
        out = []
        for ln, raw in enumerate(ctx.lines, 1):
            line = raw.rstrip("\r")
            n = len(line)
            if n <= soft or self._ignored(line):
                continue
            if n > hard:
                fixable = wrap_line(ctx, ln, hard) is not None
                out.append(self.violation(ctx, ln, hard + 1,
                                          f"line is {n} characters (max {hard})", fixable))
            elif self.opts["report_target"]:
                v = self.violation(ctx, ln, soft + 1,
                                   f"line is {n} characters (target {soft}, max {hard})", False)
                v.severity = "warning"
                out.append(v)
        return out

    def fix(self, ctx: FileContext):
        hard = int(ctx.settings["max_line_length"])
        targets = [ln for ln, raw in enumerate(ctx.lines, 1)
                   if len(raw.rstrip("\r")) > hard and not self._ignored(raw)]
        if not targets:
            return None
        new = wrap_lines(ctx.content, ctx.path, ctx.settings, only=targets)
        return new if new != ctx.content else None


# '(' after these words opens a condition or expression, not an argument list.
_NOT_ARGUMENT_LIST = {"if", "for", "while", "switch", "return", "case", "else", "do",
                      "defined", "__attribute__", "__attribute", "__asm__", "asm"}


@register_rule
class UnnecessaryWrap(Rule):
    name = "unnecessary-wrap"
    category = "layout"
    summary = "Arguments split over several lines are joined when they fit on one line."
    explanation = """
        Function arguments and parameters are only wrapped when the line
        would otherwise be too long. A call, prototype or definition whose
        arguments were split over several lines but fit on one line within
        the line limit (settings.max_line_length, 100) is joined:

            Bad:   rc = ble_gattc_write_flat(conn_handle, attr_handle,
                                             value, len, cb, NULL);
            Good:  rc = ble_gattc_write_flat(conn_handle, attr_handle, value, len, cb, NULL);

        Lists are left alone when they contain a comment or a preprocessor
        line, split string literals, or when another wrapped list starts on
        the line of the closing parenthesis. Conditions of if/for/while/
        switch and macro definitions are not affected. If an outer list does
        not fit, wrapped lists inside it are still joined when they fit.
    """
    fixable = True
    options = {"max": 0}  # 0: use settings.max_line_length

    def _candidates(self, ctx: FileContext):
        code = ctx.code_nopp
        lines = ctx.lines
        max_len = int(self.opts["max"]) or int(ctx.settings["max_line_length"])
        found = []
        for m in re.finditer(r"\(", code):
            open_ = m.start()
            ln0, _ = ctx.line_col(open_)
            close = find_matching(code, open_)
            if close is None:
                continue
            ln1, col1 = ctx.line_col(close)
            if ln1 == ln0:
                continue
            # Only argument / parameter lists: '(' after a name or ')'.
            j = open_ - 1
            while j >= 0 and code[j] in " \t":
                j -= 1
            if j < 0:
                continue
            if code[j].isalnum() or code[j] == "_":
                word = re.search(r"(\w+)$", code[:j + 1]).group(1)
                if word in _NOT_ARGUMENT_LIST:
                    continue
            elif code[j] != ")":
                continue
            span = range(ln0, ln1 + 1)
            if any(ln in ctx.comment_lines or ln in ctx.pp_lines for ln in span):
                continue
            parts = [lines[ln - 1].rstrip("\r") for ln in span]
            if any(p.lstrip().startswith('"') for p in parts[1:]):
                continue  # split string literal: kept deliberately
            rest = ctx.code_lines[ln1 - 1][col1 + 1:]
            if rest.count("(") > rest.count(")"):
                continue  # another wrapped list starts here
            joined = parts[0].rstrip()
            for p in parts[1:]:
                p = p.strip()
                if not p:
                    continue
                sep = "" if joined.endswith(("(", "[")) or p.startswith((")", "]", ",", ";")) \
                    else " "
                joined += sep + p
            if len(joined) <= max_len:
                found.append((ln0, ln1, joined))
        # Outermost first; nested lists are handled in a later pass if the
        # outer one is joined, or on their own if it does not fit.
        found.sort(key=lambda f: (f[0], -(f[1] - f[0])))
        keep, last = [], 0
        for f in found:
            if f[0] > last:
                keep.append(f)
                last = f[1]
        return keep

    def check(self, ctx: FileContext):
        return [self.violation(ctx, ln0, 1,
                               f"arguments split over {ln1 - ln0 + 1} lines fit on one line "
                               f"({len(joined)} columns); join them")
                for ln0, ln1, joined in self._candidates(ctx)]

    def fix(self, ctx: FileContext):
        found = self._candidates(ctx)
        if not found:
            return None
        lines = ctx.content.split("\n")
        for ln0, ln1, joined in reversed(found):
            eol = "\r" if lines[ln1 - 1].endswith("\r") else ""
            lines[ln0 - 1:ln1] = [joined + eol]
        return "\n".join(lines)
