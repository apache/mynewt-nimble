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
