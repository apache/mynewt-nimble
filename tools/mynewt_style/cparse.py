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
Lightweight C lexing helpers.

This is deliberately *not* a C parser. Rules work on a "masked" copy of the
source in which comments and the contents of string/char literals are
replaced by spaces. The masked text has exactly the same length and line
layout as the original, so any index found in it is valid in the original.
"""

from __future__ import annotations

import bisect
import functools
import re
from dataclasses import dataclass, field
from typing import List, Optional, Set, Tuple

IDENT_RE = re.compile(r"[A-Za-z_]\w*")

C_KEYWORDS = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", "_Bool", "_Static_assert",
    "_Noreturn", "_Alignof", "_Alignas", "_Generic", "bool",
}

CONTROL_KEYWORDS = {"if", "for", "while", "switch"}

# Compiler extensions that look like 'name(' but are never function names,
# e.g. 'struct __attribute__((packed)) {'.
ATTRIBUTE_WORDS = {"__attribute__", "__attribute", "__declspec", "__asm__", "__asm", "asm",
                   "_Alignas", "__extension__"}

# Words that can never start a type in a function signature.
NON_TYPE_WORDS = {
    "return", "typedef", "if", "else", "for", "while", "switch", "do",
    "case", "default", "goto", "break", "continue", "sizeof",
}


@dataclass
class Masked:
    """Result of masking a source file."""
    code: str                       # comments + literal contents blanked
    pp_lines: Set[int] = field(default_factory=set)       # 1-based
    comment_lines: Set[int] = field(default_factory=set)  # lines touching a comment
    line_comments: List[int] = field(default_factory=list)  # offsets of '//' comments


_TOKEN_RE = re.compile(
    r"""
      (?P<line>//(?:\\\r?\n|[^\n])*)          # // comment (may continue with \)
    | (?P<block>/\*.*?(?:\*/|\Z))           # /* comment */
    | (?P<str>"(?P<sbody>(?:\\.|[^"\\\n])*)(?P<send>"?))   # "string"
    | (?P<chr>'(?P<cbody>(?:\\.|[^'\\\n])*)(?P<cend>'?))   # 'c'
    """,
    re.S | re.X)


def _blank(text: str) -> str:
    """Spaces of the same length, keeping newlines (and CRs)."""
    return re.sub(r"[^\r\n]", " ", text)


@functools.lru_cache(maxsize=16)
def mask_source(src: str) -> Masked:
    """
    Blank out comments and the inside of string/char literals.

    Literal delimiters (the quotes) are kept so rules can still see that a
    token was there. Newlines are always preserved. Also records which lines
    belong to preprocessor directives (including backslash continuations),
    which lines contain (part of) a comment and where '//' comments start.

    Cached: the fix loop builds many contexts for identical content.
    """
    out = []
    pos = 0
    comment_spans = []
    line_comments: List[int] = []
    for m in _TOKEN_RE.finditer(src):
        out.append(src[pos:m.start()])
        text = m.group(0)
        kind = m.lastgroup
        if kind in ("line", "block"):
            out.append(_blank(text))
            comment_spans.append((m.start(), m.end()))
            if kind == "line":
                line_comments.append(m.start())
        else:
            body, end = (m.group("sbody"), m.group("send")) if kind == "str" \
                else (m.group("cbody"), m.group("cend"))
            out.append(text[0] + _blank(body) + end)
        pos = m.end()
    out.append(src[pos:])
    code = "".join(out)

    starts = line_starts_of(src)
    comment_lines: Set[int] = set()
    for s, e in comment_spans:
        first = bisect.bisect_right(starts, s)
        last = bisect.bisect_right(starts, max(s, e - 1))
        comment_lines.update(range(first, last + 1))

    pp_lines: Set[int] = set()
    raw_lines = src.split("\n")
    in_pp = False
    for ln, (raw, masked) in enumerate(zip(raw_lines, code.split("\n")), 1):
        if not in_pp and masked.lstrip(" \t").startswith("#"):
            in_pp = True
        if in_pp:
            pp_lines.add(ln)
            in_pp = raw.rstrip("\r").endswith("\\")
    return Masked(code, pp_lines, comment_lines, line_comments)


def blank_lines(text: str, line_numbers: Set[int]) -> str:
    """Replace every character of the given 1-based lines with spaces."""
    if not line_numbers:
        return text
    lines = text.split("\n")
    for ln in line_numbers:
        if 1 <= ln <= len(lines):
            lines[ln - 1] = " " * len(lines[ln - 1])
    return "\n".join(lines)


_FP_TOKEN_RE = re.compile(
    r"""
      "(?:\\.|[^"\\\n])*"?       # string literal (kept verbatim)
    | '(?:\\.|[^'\\\n])*'?       # char literal
    | \\\r?\n                     # line continuation: layout, dropped below
    | \w+                          # identifier / number
    | \.\.\.|<<=|>>=|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[*/%+\-&^|]=|\#\#
                                  # multi-char punctuators (maximal munch)
    | [^\s\w]                      # any other single character
    """,
    re.X)


@functools.lru_cache(maxsize=16)
def strip_code_tokens(src: str) -> str:
    """
    Token fingerprint of a source: comments and layout removed, tokens kept
    (separated by NUL). Two sources with the same fingerprint compile to the
    same thing, which is how the engine proves a fix is purely cosmetic.

    Line breaks matter for the preprocessor, so the start and end of every
    directive are part of the fingerprint: moving a token into or out of a
    '#if' line changes it.
    """
    pp_lines = mask_source(src).pp_lines
    no_comments = _TOKEN_RE.sub(
        lambda m: _blank(m.group(0)) if m.lastgroup in ("line", "block") else m.group(0), src)
    out: List[str] = []
    for ln, line in enumerate(no_comments.split("\n"), 1):
        in_pp = ln in pp_lines
        if in_pp and (ln - 1) not in pp_lines or in_pp and _is_directive_start(line):
            out.append("<pp")
        out.extend(t for t in _FP_TOKEN_RE.findall(line + "\n")
                   if not t.startswith(("\\\n", "\\\r")))
        if in_pp and not line.rstrip("\r").endswith("\\"):
            out.append("pp>")
    return "\0".join(out)


def _is_directive_start(line: str) -> bool:
    return line.lstrip(" \t").startswith("#")


OPEN = {"(": ")", "[": "]", "{": "}"}
CLOSE = {")": "(", "]": "[", "}": "{"}


def find_matching(code: str, open_idx: int) -> Optional[int]:
    """Index of the bracket matching code[open_idx], or None."""
    opener = code[open_idx]
    closer = OPEN[opener]
    depth = 0
    for i in range(open_idx, len(code)):
        c = code[i]
        if c == opener:
            depth += 1
        elif c == closer:
            depth -= 1
            if depth == 0:
                return i
    return None


def skip_ws(code: str, idx: int) -> int:
    """First index >= idx that is not whitespace (or len(code))."""
    n = len(code)
    while idx < n and code[idx].isspace():
        idx += 1
    return idx


def split_top_level(text: str, masked: Optional[str] = None, sep: str = ",") -> List[str]:
    """Split text on sep characters that are not nested inside brackets."""
    masked = text if masked is None else masked
    parts = []
    depth = 0
    start = 0
    for i, c in enumerate(masked):
        if c in OPEN:
            depth += 1
        elif c in CLOSE:
            depth -= 1
        elif c == sep and depth == 0:
            parts.append(text[start:i])
            start = i + 1
    parts.append(text[start:])
    return parts


# -- control statements ----------------------------------------------------

_KW_PAREN = re.compile(r"\b(if|for|while|switch)\b")


@dataclass
class Ctl:
    kw: str
    kw_pos: int              # offset of the keyword
    head_end: int            # offset just after ')' (or after 'else' / 'do')
    body: int                # offset of first non-blank char after head_end


def prev_nonspace(code: str, i: int) -> int:
    i -= 1
    while i >= 0 and code[i].isspace():
        i -= 1
    return i


def is_do_while_tail(code: str, kw_pos: int) -> bool:
    p = prev_nonspace(code, kw_pos)
    if p < 0 or code[p] != "}":
        return False
    depth = 0
    for i in range(p, -1, -1):
        if code[i] == "}":
            depth += 1
        elif code[i] == "{":
            depth -= 1
            if depth == 0:
                q = prev_nonspace(code, i)
                return q >= 1 and code[q - 1:q + 1] == "do" and \
                    (q < 2 or not (code[q - 2].isalnum() or code[q - 2] == "_"))
    return False


def control_statements(code: str) -> List[Ctl]:
    """All if/for/while/switch/else/do heads in masked, preprocessor-free code."""
    out = []
    for m in _KW_PAREN.finditer(code):
        p = skip_ws(code, m.end())
        if p >= len(code) or code[p] != "(":
            continue
        close = find_matching(code, p)
        if close is None:
            continue
        out.append(Ctl(m.group(1), m.start(), close + 1, skip_ws(code, close + 1)))
    for m in re.finditer(r"\b(else|do)\b", code):
        out.append(Ctl(m.group(1), m.start(), m.end(), skip_ws(code, m.end())))
    out.sort(key=lambda c: c.kw_pos)
    return out


@dataclass
class FuncSig:
    """A function definition or prototype found at file scope."""
    name: str
    type_line: int            # 1-based line holding the return type
    name_line: int            # 1-based line holding the function name
    name_col: int             # 0-based column of the name on name_line
    open_paren: int           # absolute index of '(' in the source
    close_paren: int          # absolute index of matching ')'
    is_definition: bool       # True if followed by '{'
    brace: Optional[int]      # absolute index of '{' for definitions

    @property
    def split(self) -> bool:
        return self.type_line != self.name_line


def _is_type_tokens(text: str) -> bool:
    """True if text consists only of identifiers and '*' (e.g. 'static int *')."""
    if not text.strip():
        return False
    if re.search(r"[^\w\s*]", text):
        return False
    words = IDENT_RE.findall(text)
    if not words or words[0] in NON_TYPE_WORDS:
        return False
    return True


def find_function_signatures(code_nopp: str, line_starts: List[int]) -> List[FuncSig]:
    """
    Find file-scope function definitions and prototypes.

    Works on masked code with preprocessor lines blanked. Only signatures
    whose first token starts in column 0 are considered, which is how file
    scope is recognised without tracking 'extern "C" {' blocks.
    """
    lines = code_nopp.split("\n")
    sigs: List[FuncSig] = []

    for idx, text in enumerate(lines):
        if not text or not (text[0].isalpha() or text[0] == "_"):
            continue
        paren = text.find("(")
        if paren < 0:
            continue
        head = text[:paren]
        if re.search(r"[^\w\s*]", head):
            continue
        tokens = re.findall(r"[A-Za-z_]\w*|\*", head)
        words = [t for t in tokens if t != "*"]
        if not words:
            continue
        name = words[-1]
        if name in C_KEYWORDS or name in ATTRIBUTE_WORDS or words[0] in NON_TYPE_WORDS:
            continue
        # The name must be the last token before '('.
        if tokens[-1] != name:
            continue

        abs_open = line_starts[idx] + paren
        close = find_matching(code_nopp, abs_open)
        if close is None:
            continue
        after = skip_ws(code_nopp, close + 1)
        nxt = code_nopp[after] if after < len(code_nopp) else ""
        if nxt not in ("{", ";"):
            continue

        name_col = head.rstrip().rfind(name)
        type_tokens = words[:-1]
        if type_tokens:
            # 'static int foo(' - all on one line.
            sigs.append(FuncSig(name, idx + 1, idx + 1, name_col, abs_open, close,
                                nxt == "{", after if nxt == "{" else None))
            continue

        # 'foo(' alone: look for a type-only line right above it.
        if name_col != 0 or idx == 0:
            continue
        prev = lines[idx - 1]
        if not prev or prev[0].isspace() or not _is_type_tokens(prev):
            continue
        if prev.rstrip().endswith(","):
            continue
        if idx >= 2:
            before = lines[idx - 2].rstrip()
            # Line two above must not continue into the type line.
            if before and before[-1] in ",=(+-|&":
                continue
        sigs.append(FuncSig(name, idx, idx + 1, 0, abs_open, close,
                            nxt == "{", after if nxt == "{" else None))
    return sigs


@dataclass
class Directive:
    """A (possibly multi-line) preprocessor directive."""
    start_line: int       # 1-based
    end_line: int         # 1-based, inclusive
    name: str             # 'define', 'include', ...


def find_directives(lines: List[str], pp_lines: Set[int]) -> List[Directive]:
    """Group consecutive preprocessor lines into directives."""
    out: List[Directive] = []
    ln = 1
    total = len(lines)
    while ln <= total:
        if ln in pp_lines and lines[ln - 1].lstrip().startswith("#"):
            start = ln
            while ln < total and lines[ln - 1].rstrip().endswith("\\"):
                ln += 1
            m = re.match(r"\s*#\s*(\w*)", lines[start - 1])
            out.append(Directive(start, ln, m.group(1) if m else ""))
        ln += 1
    return out


def line_starts_of(text: str) -> List[int]:
    """Absolute index at which each line starts."""
    starts = [0]
    for i, c in enumerate(text):
        if c == "\n":
            starts.append(i + 1)
    return starts


def leading_ws(s: str) -> str:
    return s[: len(s) - len(s.lstrip(" \t"))]


def pos_to_line_col(line_starts: List[int], idx: int) -> Tuple[int, int]:
    """0-based index -> (1-based line, 0-based column)."""
    import bisect
    li = bisect.bisect_right(line_starts, idx) - 1
    return li + 1, idx - line_starts[li]
