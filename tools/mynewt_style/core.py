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

"""Core types: Violation, FileContext, Rule and the rule registry."""

from __future__ import annotations

import importlib.util
import sys
import textwrap
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Type

from . import cparse

SEVERITIES = ("error", "warning")

HEADER_SUFFIXES = (".h", ".hpp", ".hxx")

# Directives whose text is not C code (paths, messages, ...).
NON_CODE_DIRECTIVES = ("include", "include_next", "pragma", "error", "warning", "line", "import")
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx")


class RuleError(Exception):
    """Raised for invalid rule definitions or rule configuration."""


@dataclass
class Violation:
    """A single style problem found by a rule."""
    rule: str
    path: Path
    line: int          # 1-based
    column: int        # 1-based
    message: str
    severity: str = "error"
    fixable: bool = False


# Default values for the [settings] table; rules read them via ctx.settings.
DEFAULT_SETTINGS: Dict[str, Any] = {
    "indent_width": 4,
    "tab_width": 4,
    "max_line_length": 100,
    "target_line_length": 80,
}


class FileContext:
    """
    Everything a rule needs to know about one file.

    All views are computed lazily and cached, so a rule only pays for what it
    uses. Line numbers are 1-based everywhere in the public API; ``lines`` is
    a plain 0-based Python list.
    """

    def __init__(self, path: Path, content: str, settings: Optional[Dict[str, Any]] = None):
        self.path = Path(path)
        self.content = content
        self.settings = dict(DEFAULT_SETTINGS)
        if settings:
            self.settings.update(settings)
        self._lines: Optional[List[str]] = None
        self._masked: Optional[cparse.Masked] = None
        self._code_nopp: Optional[str] = None
        self._line_starts: Optional[List[int]] = None
        self._sigs: Optional[List[cparse.FuncSig]] = None
        self._directives: Optional[List[cparse.Directive]] = None
        self._code_macros: Optional[str] = None
        self._bodies: Optional[List[tuple]] = None

    # -- raw text -----------------------------------------------------------

    @property
    def lines(self) -> List[str]:
        """Lines without their trailing '\\n' (a final empty line is dropped)."""
        if self._lines is None:
            lines = self.content.split("\n")
            if lines and lines[-1] == "":
                lines.pop()
            self._lines = lines
        return self._lines

    @property
    def line_starts(self) -> List[int]:
        """Absolute offset at which each line starts."""
        if self._line_starts is None:
            self._line_starts = cparse.line_starts_of(self.content)
        return self._line_starts

    def offset(self, line: int, col: int = 0) -> int:
        """(1-based line, 0-based column) -> absolute offset."""
        return self.line_starts[line - 1] + col

    def line_col(self, offset: int) -> tuple:
        """Absolute offset -> (1-based line, 0-based column)."""
        return cparse.pos_to_line_col(self.line_starts, offset)

    # -- masked views -------------------------------------------------------

    @property
    def masked(self) -> cparse.Masked:
        if self._masked is None:
            self._masked = cparse.mask_source(self.content)
        return self._masked

    @property
    def code(self) -> str:
        """Content with comments and literal contents replaced by spaces."""
        return self.masked.code

    @property
    def code_lines(self) -> List[str]:
        """``code`` split into lines, aligned with ``lines``."""
        return self.code.split("\n")[: len(self.lines)]

    @property
    def code_nopp(self) -> str:
        """Like ``code`` but preprocessor directives are blanked as well."""
        if self._code_nopp is None:
            self._code_nopp = cparse.blank_lines(self.code, self.masked.pp_lines)
        return self._code_nopp

    @property
    def pp_lines(self) -> Set[int]:
        """1-based line numbers that are part of a preprocessor directive."""
        return self.masked.pp_lines

    @property
    def comment_lines(self) -> Set[int]:
        """1-based line numbers that contain (part of) a comment."""
        return self.masked.comment_lines

    @property
    def code_macros(self) -> str:
        """
        Like ``code`` but directives whose text is not C (#include, #error,
        #pragma, ...) are blanked; macro bodies and #if expressions are kept.
        """
        if self._code_macros is None:
            skip: Set[int] = set()
            for d in self.directives:
                if d.name in NON_CODE_DIRECTIVES:
                    skip.update(range(d.start_line, d.end_line + 1))
            self._code_macros = cparse.blank_lines(self.code, skip)
        return self._code_macros

    @property
    def rel_path(self) -> str:
        """Path relative to the repository root (settings.repo_root), '/' separated."""
        root = self.settings.get("repo_root")
        if root:
            try:
                return self.path.resolve().relative_to(Path(root).resolve()).as_posix()
            except ValueError:
                pass
        return self.path.as_posix()

    # -- structure ----------------------------------------------------------

    @property
    def function_bodies(self) -> List[tuple]:
        """(FuncSig, open_brace, close_brace) for every function definition."""
        if self._bodies is None:
            out = []
            for sig in self.functions:
                if sig.is_definition:
                    close = cparse.find_matching(self.code_nopp, sig.brace)
                    if close is not None:
                        out.append((sig, sig.brace, close))
            self._bodies = out
        return self._bodies

    @property
    def functions(self) -> List[cparse.FuncSig]:
        """File-scope function definitions and prototypes."""
        if self._sigs is None:
            self._sigs = cparse.find_function_signatures(self.code_nopp, self.line_starts)
        return self._sigs

    @property
    def directives(self) -> List[cparse.Directive]:
        """Preprocessor directives, multi-line ones grouped together."""
        if self._directives is None:
            self._directives = cparse.find_directives(self.lines, self.pp_lines)
        return self._directives

    # -- file kind ----------------------------------------------------------

    @property
    def is_header(self) -> bool:
        return self.path.suffix in HEADER_SUFFIXES

    @property
    def is_source(self) -> bool:
        return self.path.suffix in SOURCE_SUFFIXES

    @property
    def newline(self) -> str:
        """Line terminator used by the file."""
        return "\r\n" if "\r\n" in self.content else "\n"


class Rule:
    """
    Base class for all style rules.

    Subclasses set the class attributes below and implement ``check``.
    Fixable rules also implement ``fix``. A rule is configured from the
    ``[rules.<name>]`` table of the config file: ``enabled`` and ``severity``
    are handled here; every other key must be declared in ``options``.
    """

    #: Unique id used on the command line and in the config file.
    name: str = ""
    #: Group used by rulesets ('@whitespace') and in --list-rules.
    category: str = "misc"
    #: One-line description shown by --list-rules.
    summary: str = ""
    #: Longer text shown by --explain: why the rule exists, good/bad examples.
    explanation: str = ""
    #: "error" fails the run, "warning" is only reported (unless -Werror).
    severity: str = "error"
    #: True if fix() is implemented.
    fixable: bool = False
    #: Enabled when the config file does not say otherwise.
    enabled_by_default: bool = True
    #: Tokens fix() may add or remove (e.g. ("{", "}") for adding braces).
    #: The engine rejects any fix that changes the code in any other way.
    may_insert_tokens: tuple = ()
    def normalize_token(self, token: str) -> str:
        """
        Map a code token to the form the token-safety check compares. Rules
        whose fix respells tokens without changing meaning (e.g. '10u' ->
        '10U') override this.
        """
        return token

    #: Tunable options and their defaults; overridable in the config file.
    options: Dict[str, Any] = {}

    def __init__(self, options: Optional[Dict[str, Any]] = None,
                 severity: Optional[str] = None):
        self.opts: Dict[str, Any] = dict(self.options)
        for key, value in (options or {}).items():
            if key not in self.options:
                known = ", ".join(sorted(self.options)) or "none"
                raise RuleError(f"rule '{self.name}': unknown option '{key}' "
                                f"(known options: {known})")
            self.opts[key] = value
        if severity is not None:
            if severity not in SEVERITIES:
                raise RuleError(f"rule '{self.name}': severity must be one of {SEVERITIES}")
            self.severity = severity

    def check(self, ctx: FileContext) -> List[Violation]:
        """Return the violations found in ctx."""
        raise NotImplementedError

    def fix(self, ctx: FileContext) -> Optional[str]:
        """Return the fixed content, or None if there is nothing to fix."""
        return None

    def violation(self, ctx: FileContext, line: int, column: int, message: str,
                  fixable: Optional[bool] = None) -> Violation:
        """Convenience constructor; column is 1-based."""
        return Violation(self.name, ctx.path, line, column, message, self.severity,
                         self.fixable if fixable is None else fixable)

    @classmethod
    def explain_text(cls) -> str:
        return textwrap.dedent(cls.explanation).strip("\n")


class RuleRegistry:
    """Holds every known Rule class, keyed by rule name."""

    def __init__(self):
        self._rules: Dict[str, Type[Rule]] = {}
        self._loaded_dirs: Set[Path] = set()

    def register(self, cls: Type[Rule]) -> Type[Rule]:
        if not cls.name:
            raise RuleError(f"rule class {cls.__name__} has no name")
        if not cls.summary:
            raise RuleError(f"rule '{cls.name}' has no summary")
        if cls.severity not in SEVERITIES:
            raise RuleError(f"rule '{cls.name}': invalid severity '{cls.severity}'")
        existing = self._rules.get(cls.name)
        if existing is not None and existing.__qualname__ != cls.__qualname__:
            raise RuleError(f"duplicate rule name '{cls.name}' "
                            f"({existing.__qualname__} and {cls.__qualname__})")
        self._rules[cls.name] = cls
        return cls

    def get(self, name: str) -> Optional[Type[Rule]]:
        return self._rules.get(name)

    def all(self) -> List[Type[Rule]]:
        """All rule classes sorted by category, then name."""
        return sorted(self._rules.values(), key=lambda r: (r.category, r.name))

    def names(self) -> List[str]:
        return [r.name for r in self.all()]

    def categories(self) -> Set[str]:
        return {r.category for r in self._rules.values()}

    def load_directory(self, directory: Path) -> None:
        """Import every *.py file in directory; @register_rule does the rest."""
        directory = directory.resolve()
        if directory in self._loaded_dirs or not directory.is_dir():
            return
        self._loaded_dirs.add(directory)
        for file in sorted(directory.glob("*.py")):
            if file.name.startswith("_"):
                continue
            mod_name = f"mynewt_style_rules.{file.stem}"
            if mod_name in sys.modules:
                continue
            spec = importlib.util.spec_from_file_location(mod_name, file)
            if spec is None or spec.loader is None:
                continue
            mod = importlib.util.module_from_spec(spec)
            sys.modules[mod_name] = mod
            try:
                spec.loader.exec_module(mod)
            except Exception as exc:
                del sys.modules[mod_name]
                raise RuleError(f"failed to load rules from {file}: {exc}") from exc


registry = RuleRegistry()


def register_rule(cls: Type[Rule]) -> Type[Rule]:
    """Class decorator that makes a Rule available to the tool."""
    return registry.register(cls)
