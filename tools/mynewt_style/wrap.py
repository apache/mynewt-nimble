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

"""
Conservative wrapping of over-long code lines.

Only lines that exceed the limit are touched. A line is broken after the last
',', '&&' or '||' that keeps it within the limit, and the remainder is aligned
the way Mynewt code already does it:

    rc = ble_foo(conn_handle, BLE_L2CAP_CID_ATT, buf,
                 len);                      <- aligned after '('

    rc = ble_foo(
        conn_handle, buf, len);             <- '(' ends the line: keep that indent

Comments, string literals and preprocessor lines are never wrapped. If no
safe break exists the line is left alone and stays a line-length violation.
"""

from __future__ import annotations

from typing import Iterable, List, Optional, Set

from .core import FileContext

BREAK_AFTER = (",", "&&", "||")


def _enclosing_open(code: str, idx: int) -> Optional[int]:
    """
    Innermost unmatched '(' or initializer '{' before idx within the same
    statement, or None. Stops at ';' and at block braces.
    """
    depth = 0
    i = idx - 1
    while i >= 0:
        c = code[i]
        if c in ")]}":
            depth += 1
        elif c in "([":
            if depth == 0:
                return i if c == "(" else None
            depth -= 1
        elif c == "{":
            if depth == 0:
                # A block brace ends its line; an initializer brace does not.
                rest = code[i + 1: code.find("\n", i) if "\n" in code[i:] else len(code)]
                return i if rest.strip() else None
            depth -= 1
        elif c == ";" and depth == 0:
            return None
        i -= 1
    return None


def _continuation_col(ctx: FileContext, open_idx: int) -> int:
    """Column at which text inside the bracket at open_idx is aligned."""
    code = ctx.code
    j = open_idx + 1
    while j < len(code) and code[j] in " \t":
        j += 1
    if j < len(code) and code[j] != "\n":
        return ctx.line_col(j)[1]
    # Bracket ends its line: follow the indentation of the next line.
    while j < len(code) and code[j].isspace():
        j += 1
    return ctx.line_col(j)[1] if j < len(code) else ctx.line_col(open_idx)[1] + 1


def wrap_line(ctx: FileContext, line_no: int, max_len: int) -> Optional[List[str]]:
    """Return replacement lines for line_no, or None if it cannot be wrapped."""
    line = ctx.lines[line_no - 1]
    eol = "\r" if line.endswith("\r") else ""
    line = line[: len(line) - len(eol)]
    if len(line) <= max_len or line_no in ctx.pp_lines:
        return None
    code_line = ctx.code_lines[line_no - 1]
    indent = len(line) - len(line.lstrip())

    # Candidate break points: index just after a separator followed by space.
    cands = []
    for i in range(indent, len(code_line)):
        for sep in BREAK_AFTER:
            end = i + len(sep)
            if code_line.startswith(sep, i) and end < len(code_line) and code_line[end] == " ":
                cands.append(end)
    for end in sorted(set(cands), reverse=True):
        head = line[:end].rstrip()
        if len(head) > max_len:
            continue
        rest = line[end:].strip()
        if not rest or rest.startswith(("/*", "//")):
            continue
        open_idx = _enclosing_open(ctx.code, ctx.offset(line_no, end))
        if open_idx is None:
            continue
        col = _continuation_col(ctx, open_idx)
        # Must make progress, and the first token of the rest must fit.
        first_tok = rest.split(" ", 1)[0]
        if col >= end or col + len(first_tok) > max_len:
            continue
        return [head + eol, " " * col + rest + eol]
    return None


def wrap_lines(content: str, path, settings, only: Optional[Iterable[int]] = None,
               max_iter: int = 200) -> str:
    """
    Wrap every over-long line (or just those in 'only', plus the continuation
    lines produced while wrapping them). Returns the new content.
    """
    max_len = settings.get("max_line_length", 100)
    targets: Optional[Set[int]] = set(only) if only is not None else None
    for _ in range(max_iter):
        ctx = FileContext(path, content, settings)
        changed = False
        for ln in range(1, len(ctx.lines) + 1):
            if targets is not None and ln not in targets:
                continue
            new = wrap_line(ctx, ln, max_len)
            if new is None:
                continue
            lines = content.split("\n")
            lines[ln - 1: ln] = new
            content = "\n".join(lines)
            if targets is not None:
                # Shift targets below the split and include the new line.
                targets = {t + 1 if t > ln else t for t in targets} | {ln + 1}
            changed = True
            break
        if not changed:
            break
    return content
