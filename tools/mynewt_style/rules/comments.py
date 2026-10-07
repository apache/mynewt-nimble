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

"""Comment rules."""

from mynewt_style import FileContext, Rule, register_rule


def _cpp_comments(ctx: FileContext):
    """(line, 0-based column) of each '//' comment."""
    return [ctx.line_col(off) for off in ctx.masked.line_comments]


@register_rule
class NoCppComments(Rule):
    name = "no-cpp-comments"
    category = "comments"
    summary = "Use /* C style */ comments, not // C++ style."
    explanation = """
        CODING_STANDARDS.md: "No C++ style comments allowed."

        Bad:   rc = 0; // reset
        Good:  rc = 0; /* reset */

        The fix rewrites each '//' comment into a '/* ... */' comment on the
        same line.
    """
    fixable = True

    def check(self, ctx: FileContext):
        return [self.violation(ctx, ln, col + 1, "C++ style '//' comment")
                for ln, col in _cpp_comments(ctx)]

    def fix(self, ctx: FileContext):
        found = list(_cpp_comments(ctx))
        if not found:
            return None
        lines = ctx.content.split("\n")
        for ln, col in found:
            line = lines[ln - 1]
            eol = "\r" if line.endswith("\r") else ""
            body = line[col + 2:len(line) - len(eol)].rstrip()
            if body.endswith("\\"):
                continue  # '// ... \' continues the comment; leave it to a human
            text = body.strip().replace("*/", "* /")
            lines[ln - 1] = line[:col] + (f"/* {text} */" if text else "/* */") + eol
        new = "\n".join(lines)
        return new if new != ctx.content else None


@register_rule
class CommentPlacement(Rule):
    name = "comment-placement"
    category = "comments"
    summary = "Inside functions, comments go on their own line above the code."
    explanation = """
        CODING_STANDARDS.md: "When using a single line comment, put it above
        the line of code that you intend to comment".

            Good:                           Bad:
                /* check variable */            if (a) { /* check variable */
                if (a) {

        Applies inside function bodies only; trailing comments on struct
        members, enum values and #defines (outside functions) are fine.
        Tool directives (mynewt-format: ...), fall-through markers and other
        'keep_patterns' stay where they are. The fix moves the comment to its
        own line above, at the same indentation.
    """
    fixable = True
    options = {"keep_patterns": [r"mynewt-format:", r"fall[s\- ]*thr(ough|u)", r"NOTREACHED",
                                 r"NOLINT"]}

    def _trailing(self, ctx: FileContext):
        """(line, comment_start_col, comment_text) of trailing one-line comments."""
        import re
        keep = [re.compile(p, re.I) for p in self.opts["keep_patterns"]]
        spans = []
        for _sig, open_, close in ctx.function_bodies:
            spans.append((ctx.line_col(open_)[0], ctx.line_col(close)[0]))
        if not spans:
            return []
        out = []
        for ln in sorted(ctx.comment_lines):
            if ln in ctx.pp_lines or not any(a < ln < b for a, b in spans):
                continue
            raw = ctx.lines[ln - 1].rstrip("\r")
            code = ctx.code_lines[ln - 1]
            first = len(code) - len(code.lstrip())
            if not code.strip():
                continue  # comment-only line (or part of a multi-line comment)
            m = re.search(r"/\*.*?\*/\s*$|//.*$", raw)
            if not m or code[m.start():].strip() or m.start() < first:
                continue  # comment not at the end, or not on this line alone
            if not raw[:m.start()].strip():
                continue
            # Comment must start and end on this line.
            if code[m.start():m.start() + 2] != "  ":
                continue
            text = m.group(0).strip()
            if any(k.search(text) for k in keep):
                continue
            out.append((ln, m.start(), text))
        return out

    def check(self, ctx: FileContext):
        return [self.violation(ctx, ln, col + 1, "trailing comment; put it on its own line "
                                                 "above the code")
                for ln, col, _ in self._trailing(ctx)]

    def fix(self, ctx: FileContext):
        found = self._trailing(ctx)
        if not found:
            return None
        lines = ctx.content.split("\n")
        for ln, col, text in sorted(found, reverse=True):
            line = lines[ln - 1]
            eol = "\r" if line.endswith("\r") else ""
            indent = line[:len(line) - len(line.lstrip())]
            if text.startswith("//"):
                text = "/* " + text[2:].strip() + " */"
            lines[ln - 1:ln] = [indent + text + eol, line[:col].rstrip() + eol]
        return "\n".join(lines)
