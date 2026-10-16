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

"""Whitespace rules: tabs, trailing blanks, line endings, blank lines."""

import re

from mynewt_style import FileContext, Rule, register_rule


@register_rule
class NoTabs(Rule):
    name = "no-tabs"
    category = "whitespace"
    summary = "Indent with spaces; tab characters are not allowed."
    explanation = """
        CODING_STANDARDS.md: "Code must be indented to 4 spaces, tabs should
        not be used."

        Tabs render differently in every editor, so aligned code (continuation
        lines, #define columns) falls apart. The fix expands each tab to the
        next tab stop (settings.tab_width), so text that was aligned with tabs
        stays aligned.

        Bad:   <TAB>rc = 0;
        Good:      rc = 0;
    """
    fixable = True

    def check(self, ctx: FileContext):
        return [self.violation(ctx, ln, line.index("\t") + 1, "tab character")
                for ln, line in enumerate(ctx.lines, 1) if "\t" in line]

    def fix(self, ctx: FileContext):
        if "\t" not in ctx.content:
            return None
        tw = int(ctx.settings["tab_width"])
        return "\n".join(line.expandtabs(tw) for line in ctx.content.split("\n"))


@register_rule
class TrailingWhitespace(Rule):
    name = "trailing-whitespace"
    category = "whitespace"
    summary = "No spaces or tabs at the end of a line."
    explanation = """
        CODING_STANDARDS.md: "Do not add whitespace at the end of a line."

        Trailing blanks are invisible noise in diffs. After a line continuation
        backslash they are worse: '\\ ' is not a continuation at all on some
        compilers.
    """
    fixable = True

    def check(self, ctx: FileContext):
        out = []
        for ln, line in enumerate(ctx.lines, 1):
            body = line.rstrip("\r")
            stripped = body.rstrip(" \t")
            if stripped != body:
                msg = "trailing whitespace"
                if stripped.endswith("\\"):
                    msg += " after line continuation '\\'"
                out.append(self.violation(ctx, ln, len(stripped) + 1, msg))
        return out

    def fix(self, ctx: FileContext):
        new = re.sub(r"[ \t]+(?=\r?$)", "", ctx.content, flags=re.M)
        return new if new != ctx.content else None


@register_rule
class LineEndings(Rule):
    name = "line-endings"
    category = "whitespace"
    summary = "Use Unix line endings (LF), not CRLF."
    explanation = """
        The repository uses '\\n' line endings. Files with '\\r\\n' produce
        whole-file diffs as soon as someone with a different editor touches them.
    """
    fixable = True

    def check(self, ctx: FileContext):
        for ln, line in enumerate(ctx.lines, 1):
            if line.endswith("\r"):
                return [self.violation(ctx, ln, len(line), "CRLF line endings in file")]
        return []

    def fix(self, ctx: FileContext):
        return ctx.content.replace("\r\n", "\n") if "\r\n" in ctx.content else None


@register_rule
class EofNewline(Rule):
    name = "eof-newline"
    category = "whitespace"
    summary = "Files end with exactly one newline."
    explanation = """
        A missing final newline makes 'git diff' print "\\ No newline at end of
        file" and is undefined behaviour in C89 for source files. Extra blank
        lines at the end are just noise.
    """
    fixable = True

    def check(self, ctx: FileContext):
        c = ctx.content
        if not c:
            return []
        if not c.endswith("\n"):
            return [self.violation(ctx, len(ctx.lines), len(ctx.lines[-1]) + 1,
                                   "no newline at end of file")]
        if c.endswith("\n\n") or c.endswith("\n\r\n"):
            stripped = c.rstrip("\r\n")
            first_blank = stripped.count("\n") + 2
            return [self.violation(ctx, first_blank, 1, "blank line(s) at end of file")]
        return []

    def fix(self, ctx: FileContext):
        c = ctx.content
        if not c:
            return None
        new = c.rstrip("\r\n \t") + ctx.newline if c.strip() else c
        return new if new != c else None


@register_rule
class MaxBlankLines(Rule):
    name = "max-blank-lines"
    category = "whitespace"
    summary = "No more than 'max' consecutive blank lines."
    explanation = """
        Blank lines that group code are welcome and are never removed or
        added, but long runs of empty lines are usually leftovers from editing.
        Only runs longer than the limit are shortened.
    """
    fixable = True
    options = {"max": 1}

    def _runs(self, lines):
        """Yield (first_line, length) of every blank-line run."""
        start = None
        for ln, line in enumerate(lines, 1):
            if line.strip() == "":
                if start is None:
                    start = ln
            else:
                if start is not None:
                    yield start, ln - start
                start = None
        if start is not None:
            yield start, len(lines) - start + 1

    def check(self, ctx: FileContext):
        limit = int(self.opts["max"])
        return [self.violation(ctx, start + limit, 1,
                               f"{length} consecutive blank lines (max {limit})")
                for start, length in self._runs(ctx.lines) if length > limit]

    def fix(self, ctx: FileContext):
        limit = int(self.opts["max"])
        drop = set()
        for start, length in self._runs(ctx.lines):
            if length > limit:
                drop.update(range(start + limit, start + length))
        if not drop:
            return None
        lines = ctx.content.split("\n")
        return "\n".join(l for i, l in enumerate(lines, 1) if i not in drop)
