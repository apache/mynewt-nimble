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

"""Command line interface of tools/mynewt_format.py."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import List, Optional, Set

from . import files as fsel
from .config import Config, ConfigError, find_config, load_config
from .core import RuleError, registry
from .engine import Engine, FileResult, line_opcodes
from .interactive import QuitReview, Reviewer
from .report import (Colors, display_path, explain_rule, format_problems, list_rules, list_rulesets,
                     print_diff, unified_diff)

BUILTIN_RULES_DIR = Path(__file__).resolve().parent / "rules"

EXIT_OK = 0
EXIT_FAIL = 1
EXIT_USAGE = 2

EPILOG = """\
modes:
  (default) -c   report violations
            -d   print the proposed changes as a unified diff (pipe it to 'git apply')
            -f   apply all fixes in place
            -i   walk through each proposed change and choose what to apply

examples:
  %(prog)s                          check files modified in the working tree
  %(prog)s -i                       review fixes for modified files one by one
  %(prog)s -d nimble/host/src       show proposed changes for a directory
  %(prog)s -f --staged              fix staged files (re-add them afterwards)
  %(prog)s --git-diff origin/master
                                    check the lines changed on this branch (CI)
  %(prog)s --whole-files some/file.c
                                    check every line of a file, not just changes
  %(prog)s --list-rules             print all rules with a short description
  %(prog)s --explain define-alignment
  %(prog)s --rules line-length,no-tabs -c some/file.c
  %(prog)s --ruleset whitespace -f --all

inline suppressions (in any C comment):
  mynewt-format: off / mynewt-format: on       disable a region
  mynewt-format: ignore [rule,...]             this line
  mynewt-format: ignore-next-line [rule,...]   the next line

Only lines changed according to git (committed on the branch with --git-diff,
staged, or modified in the working tree) are checked by default, so code that
predates a rule is not reported. New files are checked completely.

exit status: 0 clean, 1 problems found (or changes proposed with -d), 2 usage error
"""


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="mynewt_format.py",
        description="Coding style checker and fixer for Apache Mynewt projects.",
        epilog=EPILOG,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    mode = p.add_argument_group("mode").add_mutually_exclusive_group()
    mode.add_argument("-c", "--check", action="store_const", dest="mode", const="check",
                      help="report violations without changing anything (default)")
    mode.add_argument("-d", "--diff", action="store_const", dest="mode", const="diff",
                      help="show the proposed changes as a unified diff")
    mode.add_argument("-f", "--fix", action="store_const", dest="mode", const="fix",
                      help="apply all fixes in place")
    mode.add_argument("-i", "--interactive", action="store_const", dest="mode",
                      const="interactive", help="ask before applying each change")

    sel = p.add_argument_group("file selection")
    sel.add_argument("paths", nargs="*", type=Path, help="files or directories to process")
    which = sel.add_mutually_exclusive_group()
    which.add_argument("--all", action="store_true", help="every C file in the repository")
    which.add_argument("--modified", action="store_true",
                       help="staged, unstaged and untracked files (default without paths)")
    which.add_argument("--staged", action="store_true", help="files staged for commit")
    which.add_argument("--git-diff", metavar="REF",
                       help="files changed since merge-base of REF and HEAD")
    scope = sel.add_mutually_exclusive_group()
    scope.add_argument("--changed-lines", action="store_true",
                       help="only report and fix lines changed according to git (default)")
    scope.add_argument("--whole-files", action="store_true",
                       help="report and fix every line of the selected files, not just "
                            "the changed ones (implied by --all)")

    rules = p.add_argument_group("rules")
    rules.add_argument("--ruleset", metavar="NAME", help="ruleset from the config to use")
    rules.add_argument("--rules", metavar="LIST",
                       help="comma separated rules/@categories to run instead of a ruleset")
    rules.add_argument("--disable", metavar="LIST",
                       help="comma separated rules/@categories to skip")
    rules.add_argument("--rules-dir", metavar="DIR", type=Path, action="append", default=[],
                       help="extra directory with rule plugins (repeatable)")
    rules.add_argument("--list-rules", action="store_true",
                       help="list all rules (add -v for full explanations)")
    rules.add_argument("--list-rulesets", action="store_true", help="list configured rulesets")
    rules.add_argument("--explain", metavar="RULE", help="explain a rule with examples")

    gen = p.add_argument_group("general")
    gen.add_argument("--config", type=Path, metavar="FILE",
                     help="config file (default: nearest newt-coding-rules)")
    gen.add_argument("-W", "--werror", action="store_true", help="treat warnings as errors")
    gen.add_argument("--report", choices=("code", "list"), default="code",
                     help="'code' (default) shows each problem with its source line and the "
                          "proposed fix; 'list' prints one line per problem")
    gen.add_argument("--format", choices=("text", "github"), default="text",
                     help="diagnostic format; 'github' emits workflow annotations")
    gen.add_argument("--no-color", action="store_true", help="disable colored output")
    gen.add_argument("-q", "--quiet", action="store_true", help="only print problems")
    gen.add_argument("-v", "--verbose", action="store_true", help="print more details")
    gen.add_argument("--install-hook", action="store_true",
                     help="install a git pre-commit hook that checks staged changes")
    return p


def _split(value: Optional[str]) -> List[str]:
    return [x.strip() for x in (value or "").split(",") if x.strip()]


def _err(msg: str) -> None:
    print(f"{Colors.RED}mynewt_format: {msg}{Colors.RESET}", file=sys.stderr)


def install_hook(repo: Path) -> int:
    hooks = repo / ".git" / "hooks"
    if not hooks.is_dir():
        _err(f"no .git/hooks directory in {repo}")
        return EXIT_USAGE
    hook = hooks / "pre-commit"
    marker = "# mynewt_format pre-commit hook"
    if hook.exists() and marker not in hook.read_text(errors="replace"):
        _err(f"{hook} already exists and was not created by this tool; "
             f"add 'python3 tools/mynewt_format.py --staged' to it manually")
        return EXIT_USAGE
    hook.write_text(f"""#!/bin/sh
{marker}
# Checks only the lines you changed; fix them with:
#   python3 tools/mynewt_format.py -i --staged
exec python3 "$(git rev-parse --show-toplevel)/tools/mynewt_format.py" \\
    --staged --check
""")
    hook.chmod(0o755)
    print(f"installed {hook}")
    return EXIT_OK


def load_everything(args) -> Config:
    cwd = Path.cwd()
    repo = fsel.find_repo_root(cwd)
    cfg_path = args.config or find_config(cwd) or find_config(repo)
    config = load_config(cfg_path, repo)
    base = cfg_path.parent if cfg_path else repo
    registry.load_directory(BUILTIN_RULES_DIR)
    for d in config.settings.get("rule_dirs", []):
        registry.load_directory((base / d) if not Path(d).is_absolute() else Path(d))
    for d in args.rules_dir:
        registry.load_directory(d)
    config.validate()
    return config


def select_files(args, config: Config, repo: Path):
    """Returns (files, git_selection or None)."""
    exts = list(config.settings["extensions"])
    ignore_file = Path(config.settings["ignore_file"])
    base = config.path.parent if config.path else repo
    ignore = fsel.load_ignore_patterns(ignore_file if ignore_file.is_absolute()
                                       else base / ignore_file)
    git = None
    if args.paths:
        if not args.whole_files:
            git = fsel.GitSelection(repo, "ref", args.git_diff) if args.git_diff \
                else fsel.GitSelection(repo, "staged" if args.staged else "worktree")
        return fsel.collect_paths(args.paths, repo, exts, ignore, explicit=True), git
    if args.all:
        return fsel.collect_paths([repo], repo, exts, ignore, explicit=False), None
    if args.git_diff:
        git = fsel.GitSelection(repo, "ref", args.git_diff)
    elif args.staged:
        git = fsel.GitSelection(repo, "staged")
    else:
        git = fsel.GitSelection(repo, "worktree")
    return fsel.collect_paths(git.files(), repo, exts, ignore, explicit=False), git


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    args.mode = args.mode or "check"

    if args.no_color or os.environ.get("NO_COLOR") or not sys.stdout.isatty() \
            or args.format == "github":
        Colors.disable()

    repo = fsel.find_repo_root(Path.cwd())
    if args.install_hook:
        return install_hook(repo)

    try:
        config = load_everything(args)
    except (ConfigError, RuleError) as exc:
        _err(str(exc))
        return EXIT_USAGE

    if args.list_rules:
        list_rules(registry.all(), config, args.verbose)
        return EXIT_OK
    if args.list_rulesets:
        list_rulesets(config)
        return EXIT_OK
    if args.explain:
        cls = registry.get(args.explain)
        if cls is None:
            _err(f"unknown rule '{args.explain}' (see --list-rules)")
            return EXIT_USAGE
        explain_rule(cls, config)
        return EXIT_OK

    if args.changed_lines and args.all:
        _err("--changed-lines cannot be combined with --all (which checks whole files)")
        return EXIT_USAGE
    if args.all:
        args.whole_files = True
    if not args.whole_files and not (repo / ".git").exists():
        # No history to compare against: the only sensible scope is everything.
        args.whole_files = True
    if args.mode == "interactive" and not sys.stdin.isatty():
        _err("--interactive needs a terminal on stdin")
        return EXIT_USAGE

    try:
        rules = config.build_rules(args.ruleset, _split(args.rules), _split(args.disable))
        files, git = select_files(args, config, repo)
    except (ConfigError, fsel.GitError) as exc:
        _err(str(exc))
        return EXIT_USAGE

    if not rules:
        _err("no rules selected")
        return EXIT_USAGE
    if not files:
        if not args.quiet:
            print("No matching files to process.")
        return EXIT_OK

    if args.verbose:
        cfg_name = config.path or "<built-in defaults>"
        print(f"config: {cfg_name}", file=sys.stderr)
        print(f"rules:  {', '.join(r.name for r in rules)}", file=sys.stderr)
        print(f"files:  {len(files)}", file=sys.stderr)

    return run(args, config, rules, files, git, repo)


def run(args, config: Config, rules, files: List[Path], git, repo: Path) -> int:
    engine = Engine(rules, dict(config.settings, repo_root=str(repo)))
    reviewer = Reviewer(repo) if args.mode == "interactive" else None
    diag_out = sys.stderr if args.mode == "diff" else sys.stdout

    errors = warnings = fixable = 0
    limited = 0   # files checked on changed lines only
    files_bad = files_changed = 0
    internal_problems = 0
    # Fixes are written to the working tree, so line numbers must come from it.
    writes = args.mode in ("fix", "interactive")
    line_source = fsel.GitSelection(repo, "worktree") \
        if git is not None and git.mode == "staged" and writes else git

    for path in files:
        try:
            if git is not None and git.mode == "staged" and args.mode in ("check", "diff"):
                content = git.staged_content(path)
            else:
                content = path.read_text(encoding="utf-8", errors="surrogateescape")
        except (OSError, fsel.GitError) as exc:
            _err(f"{path}: {exc}")
            internal_problems += 1
            continue

        changed: Optional[Set[int]] = None
        if not args.whole_files and line_source is not None:
            try:
                changed = line_source.changed_lines(path)
            except fsel.GitError as exc:
                _err(str(exc))
                internal_problems += 1
                continue

        if changed is not None:
            limited += 1
        result = engine.process(path, content, changed)
        for e in result.errors:
            _err(f"{path}: {e}")
            internal_problems += 1

        remaining = result.violations
        shown, hunks = content, result.hunks   # what the violations refer to
        if args.mode == "diff":
            if result.changed:
                files_changed += 1
                print_diff(unified_diff(result.original, result.proposed, path, repo))
            remaining = _left_after(result, result.proposed, engine, changed)
            shown, hunks = result.proposed, []
        elif args.mode in ("fix", "interactive"):
            new = None
            if result.changed:
                if reviewer is not None:
                    try:
                        new = reviewer.review(result)
                    except QuitReview as q:
                        if q.content is not None:
                            _write(path, q.content)
                            files_changed += 1
                        break
                else:
                    new = result.proposed
            if new is not None and new != content:
                _write(path, new)
                files_changed += 1
                if not args.quiet:
                    print(f"{Colors.GREEN}fixed{Colors.RESET} {display_path(path, repo)}")
            shown, hunks = (new if new is not None else content), []
            remaining = _left_after(result, shown, engine, changed)

        if remaining:
            files_bad += 1
            for line in format_problems(remaining, shown, hunks, repo, args.format, args.report,
                                        int(config.settings.get("tab_width", 4))):
                print(line, file=diag_out)
            if args.report == "code":
                print(file=diag_out)
        for v in remaining:
            if v.severity == "error":
                errors += 1
            else:
                warnings += 1
            if v.fixable:
                fixable += 1

    if writes and files_changed and git is not None and git.mode == "staged" \
            and not args.quiet:
        print("Fixed files are not re-staged; review and 'git add' them.")
    args.limited = limited
    return _summary(args, errors, warnings, fixable, files_bad, files_changed,
                    len(files), internal_problems, diag_out)


def _left_after(result: FileResult, content: str, engine: Engine,
                changed: Optional[Set[int]]):
    """Violations that remain in 'content' after (some of) the fixes."""
    if content == result.original:
        return result.violations
    violations, _ = engine.check(result.path, content)
    if changed is None:
        return violations
    mapped = map_changed_lines(result.original, content, changed)
    return [v for v in violations if v.line in mapped]


def map_changed_lines(original: str, new: str, changed: Set[int]) -> Set[int]:
    """Carry a set of changed line numbers over an edit; edited lines count as changed."""
    a = original.splitlines(keepends=True)
    b = new.splitlines(keepends=True)
    out: Set[int] = set()
    for tag, i1, i2, j1, j2 in line_opcodes(a, b):
        if tag == "equal":
            out.update(j1 + k + 1 for k in range(i2 - i1) if i1 + k + 1 in changed)
        else:
            out.update(range(j1 + 1, j2 + 1))
    return out


def _write(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8", errors="surrogateescape", newline="")


def _summary(args, errors, warnings, fixable, files_bad, files_changed, total,
             internal, out) -> int:
    failed = errors > 0 or (args.werror and warnings > 0) or internal > 0
    if args.mode == "diff" and files_changed:
        failed = True
    if args.quiet and not failed:
        return EXIT_OK
    parts = []
    if args.mode in ("fix", "interactive"):
        parts.append(f"{files_changed} file(s) changed")
    elif args.mode == "diff":
        parts.append(f"{files_changed} file(s) would change")
    parts.append(f"{errors} error(s), {warnings} warning(s) in {files_bad} of {total} file(s)")
    if getattr(args, "limited", 0):
        parts[-1] += " (changed lines only; --whole-files checks everything)"
    color = Colors.RED if failed else (Colors.YELLOW if warnings else Colors.GREEN)
    print(f"{color}{'; '.join(parts)}{Colors.RESET}", file=out)
    if args.mode == "check" and fixable:
        print(f"{fixable} of them can be fixed automatically: rerun with -d to preview, "
              f"-i to choose, or -f to apply.", file=out)
    return EXIT_FAIL if failed else EXIT_OK
