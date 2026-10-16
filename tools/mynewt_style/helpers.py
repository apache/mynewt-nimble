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

"""Building blocks shared by rule plugins."""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Iterator, List, Optional

from .core import FileContext, Rule

# C punctuators, longest first (maximal munch), plus words.
TOKEN_RE = re.compile(
    r"\w+|\.\.\.|<<=|>>=|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[*/%+\-&^|]=|##|[^\s\w]")


@dataclass
class Tok:
    text: str
    start: int
    end: int


def tokens(code: str, start: int = 0, end: Optional[int] = None) -> List[Tok]:
    """Tokens of (masked) code between start and end, with absolute offsets."""
    end = len(code) if end is None else end
    return [Tok(m.group(0), m.start(), m.end()) for m in TOKEN_RE.finditer(code, start, end)]


@dataclass
class Edit:
    """Replace content[start:end] by text. Offsets are absolute."""
    start: int
    end: int
    text: str
    message: str
    fixable: bool = True


def is_blank(ctx: FileContext, start: int, end: int) -> bool:
    """True if the *original* text in [start, end) is only whitespace (no comment)."""
    return ctx.content[start:end].strip() == ""


class EditRule(Rule):
    """
    A rule expressed as a list of text edits. Subclasses implement edits();
    check() and fix() come for free. Non-fixable findings use fixable=False
    (their text is ignored).
    """

    def edits(self, ctx: FileContext) -> Iterator[Edit]:
        raise NotImplementedError

    def check(self, ctx: FileContext):
        out = []
        for e in self.edits(ctx):
            ln, col = ctx.line_col(e.start)
            out.append(self.violation(ctx, ln, col + 1, e.message,
                                      fixable=self.fixable and e.fixable))
        return out

    def fix(self, ctx: FileContext):
        edits = sorted((e for e in self.edits(ctx) if e.fixable),
                       key=lambda e: (e.start, e.end))
        if not edits:
            return None
        out = []
        pos = 0
        for e in edits:
            if e.start < pos:
                continue  # overlapping edit; the next pass picks it up
            out.append(ctx.content[pos:e.start])
            out.append(e.text)
            pos = e.end
        out.append(ctx.content[pos:])
        new = "".join(out)
        return new if new != ctx.content else None
