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

"""Declarations and naming."""

import re
from dataclasses import dataclass, field
from typing import List, Optional

from mynewt_style import FileContext, Rule, register_rule
from mynewt_style.cparse import C_KEYWORDS, find_matching, skip_ws
from mynewt_style.files import is_ignored

_STATEMENT_WORDS = {"return", "goto", "break", "continue", "if", "else", "for", "while",
                    "do", "switch", "case", "default", "sizeof", "typedef"}

_DECL_RE = re.compile(r"""
    ^(?:(?:static|const|volatile|register|extern|inline|_Atomic|signed|unsigned|short|long)\s+)*
    (?P<type>(?:struct|union|enum)\s+\w+|\w+)
    (?:\s+[A-Za-z_]\w*)*?                 # more type words / attribute macros

    [\s*]+(?:const\s+)?
    (?:\(\s*\*\s*)?\w+
    \s*(?:\)\s*\(|\[|=|,|$)
""", re.X | re.S)

_LABEL_RE = re.compile(r"^\s*(?:case\b[^:]*:|default\s*:|[A-Za-z_]\w*\s*:(?!:))\s*")


@dataclass
class Item:
    kind: str                  # 'decl' | 'stmt' | 'block'
    start: int                 # offset of first char
    end: int                   # offset just past ';' or '}'
    header: str = ""           # for blocks: text before '{' ('if (x)', 'else', '')
    children: List["Item"] = field(default_factory=list)


def classify(text: str) -> str:
    t = text.strip()
    while True:
        m = _LABEL_RE.match(t)
        if not m or m.end() == 0:
            break
        t = t[m.end():]
    first = re.match(r"\w+", t)
    if not first or first.group(0) in _STATEMENT_WORDS:
        return "stmt"
    if re.match(r"(?:(?:static|const|volatile)\s+)*(?:struct|union|enum)\s*(?:\w+\s*)?\{", t):
        return "decl"  # local (possibly anonymous) struct definition
    return "decl" if _DECL_RE.match(t) else "stmt"


def walk(code: str, open_: int, close: int) -> List[Item]:
    """Statements of the block code[open_] .. code[close] (braces)."""
    items: List[Item] = []
    i = open_ + 1
    while True:
        i = skip_ws(code, i)
        if i >= close:
            break
        j = i
        depth = 0
        while j < close:
            c = code[j]
            if c in "([":
                depth += 1
            elif c in ")]":
                depth -= 1
            elif depth == 0 and c == ";":
                break
            elif depth == 0 and c == "{":
                head = code[i:j].rstrip()
                if head.endswith(("=", ",")) or re.match(
                        r"(?:typedef\s+)?(?:(?:static|const)\s+)*(?:struct|union|enum)\b[^()]*$",
                        head.strip()):
                    # Initializer or local struct definition, part of a declaration.
                    end = find_matching(code, j)
                    if end is None:
                        return items
                    j = end
                else:
                    break
            j += 1
        if j >= close:
            items.append(Item("stmt", i, close))
            break
        if code[j] == ";":
            items.append(Item(classify(code[i:j]), i, j + 1))
            i = j + 1
        else:
            end = find_matching(code, j)
            if end is None:
                break
            items.append(Item("block", i, end + 1, code[i:j].strip(), walk(code, j, end)))
            i = end + 1
    return items


def all_blocks(items: List[Item]):
    """Yield every nested block's item list (not including the top one)."""
    for it in items:
        if it.kind == "block":
            yield it
            yield from all_blocks(it.children)


_FOR_DECL_RE = re.compile(r"^for\s*\(\s*(?:const\s+)?(?:struct\s+)?\w+[\s*]+\w+\s*[=;]")


@register_rule
class DeclarationsAtTop(Rule):
    name = "declarations-at-top"
    category = "declarations"
    summary = "Local variables are declared at the top of the function, before any statement."
    explanation = """
        CODING_STANDARDS.md: "Place all function-local variable definitions at
        the top of the function body, before any statements."

            Good:                           Bad:
                int rc;                         ble_foo_init();
                                                int rc = ble_foo();
                ble_foo_init();
                rc = ble_foo();

        An early 'return' wrapped in #if/#endif before the declarations (used
        to compile a function out when a feature is disabled) is allowed.
        Loop variables declared in a 'for' header are reported too, unless
        'allow_for_init' is set. With 'allow_block_scope = true' a nested
        block may also declare variables at its own top (C89 style).
        Not auto-fixable: moving a declaration with an initializer would
        change the order of evaluation.
    """
    options = {"allow_block_scope": False, "allow_for_init": False}

    @staticmethod
    def _branch_lines(ctx: FileContext):
        """Lines of #else/#elif: alternative code starts again from the top."""
        return {d.start_line for d in ctx.directives if d.name in ("else", "elif")}

    @staticmethod
    def _conditional_lines(ctx: FileContext):
        """1-based lines inside an #if/#ifdef/#ifndef ... #endif region."""
        inside = set()
        depth = 0
        start = {}
        for d in ctx.directives:
            if d.name in ("if", "ifdef", "ifndef"):
                depth += 1
                start[depth] = d.end_line
            elif d.name == "endif" and depth:
                inside.update(range(start[depth] + 1, d.start_line))
                depth -= 1
        return inside

    def check(self, ctx: FileContext):
        code = ctx.code_nopp
        out = []
        conditional = self._conditional_lines(ctx)
        branches = self._branch_lines(ctx)

        def early_return(it: Item) -> bool:
            # '#if !FEATURE / return BLE_HS_ENOTSUP; / #endif' before the
            # declarations is the accepted way to compile a function out.
            return (re.match(r"\s*return\b", code[it.start:it.end]) is not None
                    and ctx.line_col(it.start)[0] in conditional)

        def report(off: int, msg: str):
            ln, col = ctx.line_col(off)
            out.append(self.violation(ctx, ln, col + 1, msg))

        def scan(items: List[Item], top: bool):
            seen_stmt = False
            prev_end_line = 0
            for it in items:
                start_line = ctx.line_col(it.start)[0]
                if any(prev_end_line < b < start_line for b in branches):
                    seen_stmt = False  # '#else' alternative of the code above
                prev_end_line = ctx.line_col(it.end - 1)[0]
                if it.kind == "decl":
                    if seen_stmt:
                        report(it.start, "declaration after a statement; move it to the top "
                                         "of the function")
                    elif not top and not self.opts["allow_block_scope"]:
                        report(it.start, "declaration inside a nested block; move it to the "
                                         "top of the function")
                elif not (top and early_return(it)):
                    seen_stmt = True
                if it.kind == "block":
                    if _FOR_DECL_RE.match(it.header) and not self.opts["allow_for_init"]:
                        report(it.start, "loop variable declared in 'for'; declare it at the "
                                         "top of the function")
                    scan(it.children, False)

        for _sig, open_, close in ctx.function_bodies:
            scan(walk(code, open_, close), True)
        return out


@register_rule
class BlankLineAfterDeclarations(Rule):
    name = "blank-line-after-declarations"
    category = "declarations"
    summary = "One blank line separates local declarations from the first statement."
    explanation = """
        Linux checkpatch LINE_SPACING; also the prevailing Mynewt layout.

            Good:                       Bad:
                struct os_mbuf *om;         struct os_mbuf *om;
                int rc;                     int rc;
                                            om = ble_hs_mbuf_att_pkt();
                om = ble_hs_mbuf_att_pkt();

        The fix inserts the blank line.
    """
    fixable = True

    def _spots(self, ctx: FileContext):
        """1-based line numbers after which a blank line is missing."""
        code = ctx.code_nopp
        out = []

        def scan(items: List[Item]):
            n = 0
            while n < len(items) and items[n].kind == "decl":
                n += 1
            if 0 < n < len(items):
                end_line = ctx.line_col(items[n - 1].end - 1)[0]
                next_line = ctx.line_col(items[n].start)[0]
                between = ctx.lines[end_line:next_line - 1]
                if next_line > end_line and not any(not l.strip() for l in between):
                    out.append(end_line)
            for it in items:
                if it.kind == "block":
                    scan(it.children)

        for _sig, open_, close in ctx.function_bodies:
            scan(walk(code, open_, close))
        return out

    def check(self, ctx: FileContext):
        return [self.violation(ctx, ln + 1, 1, "missing blank line after declarations")
                for ln in self._spots(ctx)]

    def fix(self, ctx: FileContext):
        spots = set(self._spots(ctx))
        if not spots:
            return None
        lines = ctx.content.split("\n")
        out = []
        for ln, line in enumerate(lines, 1):
            out.append(line)
            if ln in spots:
                out.append("")
        return "\n".join(out)


@register_rule
class FunctionNaming(Rule):
    name = "function-naming"
    category = "declarations"
    summary = "Function names are lower case; global ones start with their module prefix."
    explanation = """
        CODING_STANDARDS.md: "Names of functions, structures and variables
        must be in all lowercase" and "Globally visible names must be prefixed
        with the name of the module".

            Good:  int ble_gap_connect(...);     static void conn_cb(...);
            Bad:   int BleGapConnect(...);       int gap_connect(...);  (global, no prefix)

        'prefixes' maps path globs (first match wins) to the prefixes allowed
        for non-static functions defined there; an empty list disables the
        prefix check for that path. Names matching 'exempt' (e.g. vendor
        interrupt handlers) are skipped.
    """
    # Prefixes are repository specific; each repo sets them in its config.
    options = {
        "prefixes": {},
        # Names fixed by someone else: main, interrupt vectors, CMSIS system
        # functions, ST HAL callbacks, TinyUSB callbacks.
        "exempt": [r"^main$", r"_IRQHandler$", r"_Handler$", r"^System[A-Z]\w*$",
                   r"^HAL_\w+$", r"^tud_"],
    }

    def check(self, ctx: FileContext):
        if not ctx.is_source:
            return []
        rel = ctx.rel_path
        prefixes = None
        for glob, allowed in self.opts["prefixes"].items():
            if is_ignored(rel, [glob]):
                prefixes = allowed
                break
        out = []
        for sig in ctx.functions:
            if not sig.is_definition:
                continue
            name = sig.name
            if any(re.search(p, name) for p in self.opts["exempt"]):
                continue
            if name != name.lower():
                out.append(self.violation(ctx, sig.name_line, sig.name_col + 1,
                                          f"function name '{name}' is not lower case"))
                continue
            head = ctx.lines[sig.type_line - 1]
            if prefixes and not re.match(r"\s*static\b", head) \
                    and not name.startswith(tuple(prefixes)):
                out.append(self.violation(
                    ctx, sig.name_line, sig.name_col + 1,
                    f"global function '{name}' should start with {' or '.join(prefixes)} "
                    f"(or be static)"))
        return out


@register_rule
class TypedefSuffix(Rule):
    name = "typedef-suffix"
    category = "declarations"
    summary = "Non-struct typedef names end in _t (or _fn for function types)."
    explanation = """
        CODING_STANDARDS.md: "Indicate typedefs by applying the _t marker to
        them." Function (pointer) types are named with '_fn' or '_cb'.

            Good:  typedef uint32_t ble_npl_time_t;
                   typedef int ble_gap_event_fn(struct ble_gap_event *ev, void *arg);
            Bad:   typedef uint32_t ble_npl_time;
    """
    severity = "warning"   # renaming a public type is an API change
    options = {"suffixes": ["_t", "_fn", "_cb"]}

    def check(self, ctx: FileContext):
        code = ctx.code_nopp
        out = []
        for m in re.finditer(r"\btypedef\b", code):
            end = m.end()
            depth = 0
            while end < len(code):
                c = code[end]
                if c in "({[":
                    depth += 1
                elif c in ")}]":
                    depth -= 1
                elif c == ";" and depth == 0:
                    break
                end += 1
            stmt = code[m.end():end]
            if re.match(r"\s*struct\b", stmt):
                continue  # no-struct-typedef handles these
            flat = re.sub(r"\{.*\}", " ", stmt, flags=re.S)
            name = None
            fp = re.search(r"\(\s*\*?\s*(\w+)\s*\)\s*\(", flat)
            if fp:
                name = fp.group(1)
            else:
                fn = re.search(r"(\w+)\s*\(", flat)
                if fn and fn.group(1) not in C_KEYWORDS:
                    name = fn.group(1)
                else:
                    words = re.findall(r"[A-Za-z_]\w*", re.sub(r"\[.*?\]", "", flat))
                    name = words[-1] if words else None
            if name and not name.endswith(tuple(self.opts["suffixes"])):
                ln, col = ctx.line_col(m.start())
                out.append(self.violation(
                    ctx, ln, col + 1,
                    f"typedef '{name}' should end with {' or '.join(self.opts['suffixes'])}"))
        return out
