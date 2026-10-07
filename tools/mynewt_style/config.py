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
Loading of the 'newt-coding-rules' configuration file (TOML).

    [settings]            global knobs shared by all rules
    [rulesets]            named selections of rules
    [rules.<rule-name>]   enabled / severity / rule specific options

Ruleset entries:
    "*"              every rule that is enabled in the config
    "@category"      every enabled rule of a category
    "rule-name"      that rule, even if it is disabled by default
    "ruleset:other"  everything selected by another ruleset
    "-<entry>"       remove what <entry> selects
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Set

from .core import DEFAULT_SETTINGS, Rule, RuleError, registry

try:
    import tomllib
except ModuleNotFoundError:  # Python < 3.11
    try:
        import tomli as tomllib  # type: ignore
    except ModuleNotFoundError:
        tomllib = None  # type: ignore

CONFIG_NAMES = ("newt-coding-rules", ".newt-coding-rules", "newt-coding-rules.toml")

ENGINE_SETTINGS: Dict[str, Any] = {
    "extensions": [".c", ".h"],
    "rule_dirs": [],
    "ignore_file": "newt-coding-rules-ignore",
    "fix_passes": 5,
    "default_ruleset": "default",
}


class ConfigError(Exception):
    pass


@dataclass
class Config:
    path: Optional[Path]
    settings: Dict[str, Any] = field(default_factory=dict)
    rulesets: Dict[str, List[str]] = field(default_factory=dict)
    rules: Dict[str, Dict[str, Any]] = field(default_factory=dict)

    # -- rule selection -----------------------------------------------------

    def is_enabled(self, name: str) -> bool:
        cls = registry.get(name)
        cfg = self.rules.get(name, {})
        return bool(cfg.get("enabled", cls.enabled_by_default if cls else False))

    def _expand(self, entry: str, stack: List[str]) -> Set[str]:
        if entry == "*":
            return {n for n in registry.names() if self.is_enabled(n)}
        if entry.startswith("@"):
            cat = entry[1:]
            if cat not in registry.categories():
                raise ConfigError(f"unknown rule category '{cat}' "
                                  f"(known: {', '.join(sorted(registry.categories()))})")
            return {r.name for r in registry.all() if r.category == cat and self.is_enabled(r.name)}
        if entry.startswith("ruleset:"):
            return self.resolve_ruleset(entry[len("ruleset:"):], stack)
        if registry.get(entry) is None:
            raise ConfigError(f"unknown rule '{entry}' (see --list-rules)")
        return {entry}

    def resolve_ruleset(self, name: str, stack: Optional[List[str]] = None) -> Set[str]:
        stack = stack or []
        if name in stack:
            raise ConfigError(f"ruleset cycle: {' -> '.join(stack + [name])}")
        if name not in self.rulesets:
            if name == "default":
                return self._expand("*", stack)
            known = ", ".join(sorted(self.rulesets)) or "none"
            raise ConfigError(f"unknown ruleset '{name}' (known: {known})")
        selected: Set[str] = set()
        for entry in self.rulesets[name]:
            if entry.startswith("-"):
                selected -= self._expand(entry[1:], stack + [name])
            else:
                selected |= self._expand(entry, stack + [name])
        return selected

    def build_rules(self, ruleset: Optional[str] = None, only: Optional[List[str]] = None,
                    disable: Optional[List[str]] = None) -> List[Rule]:
        """Instantiate the rules selected by ruleset / --rules / --disable."""
        if only:
            names: Set[str] = set()
            for entry in only:
                names |= self._expand(entry, [])
        else:
            names = self.resolve_ruleset(ruleset or self.settings["default_ruleset"])
        for entry in disable or []:
            names -= self._expand(entry, [])

        rules = []
        for cls in registry.all():
            if cls.name not in names:
                continue
            cfg = dict(self.rules.get(cls.name, {}))
            cfg.pop("enabled", None)
            severity = cfg.pop("severity", None)
            try:
                rules.append(cls(cfg, severity))
            except RuleError as exc:
                raise ConfigError(str(exc)) from exc
        return rules

    def validate(self) -> None:
        for name in self.rules:
            if registry.get(name) is None:
                raise ConfigError(f"[rules.{name}]: unknown rule (see --list-rules)")
        for name in self.rulesets:
            self.resolve_ruleset(name)
        # Instantiating every rule validates all option names.
        for cls in registry.all():
            cfg = dict(self.rules.get(cls.name, {}))
            cfg.pop("enabled", None)
            sev = cfg.pop("severity", None)
            try:
                cls(cfg, sev)
            except RuleError as exc:
                raise ConfigError(str(exc)) from exc


def find_config(start: Path) -> Optional[Path]:
    for directory in [start, *start.parents]:
        for name in CONFIG_NAMES:
            p = directory / name
            if p.is_file():
                return p
    return None


def load_config(path: Optional[Path], repo_root: Path) -> Config:
    """Load the config file (or built-in defaults when path is None)."""
    data: Dict[str, Any] = {}
    if path is not None:
        if tomllib is None:
            raise ConfigError("reading the config needs Python 3.11+ (or 'pip install tomli')")
        try:
            with open(path, "rb") as f:
                data = tomllib.load(f)
        except (OSError, tomllib.TOMLDecodeError) as exc:
            raise ConfigError(f"{path}: {exc}") from exc

    unknown = set(data) - {"settings", "rulesets", "rules"}
    if unknown:
        raise ConfigError(f"{path}: unknown table(s): {', '.join(sorted(unknown))}")

    settings = dict(DEFAULT_SETTINGS)
    settings.update(ENGINE_SETTINGS)
    user_settings = data.get("settings", {})
    for key in user_settings:
        if key not in settings:
            raise ConfigError(f"{path}: [settings] unknown key '{key}' "
                              f"(known: {', '.join(sorted(settings))})")
    settings.update(user_settings)

    rulesets = {k: list(v) for k, v in data.get("rulesets", {}).items()}
    rules = {k: dict(v) for k, v in data.get("rules", {}).items()}
    return Config(path, settings, rulesets, rules)
