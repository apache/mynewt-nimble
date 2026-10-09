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

"""File selection: explicit paths, the whole repo, or what git says changed."""

from __future__ import annotations

import fnmatch
import os
import re
import subprocess
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set

ALWAYS_IGNORED = [".git/**"]


class GitError(Exception):
    pass


def find_repo_root(start: Path) -> Path:
    for d in [start, *start.parents]:
        if (d / ".git").exists():
            return d
    return start


def load_ignore_patterns(path: Path) -> List[str]:
    patterns = list(ALWAYS_IGNORED)
    if path.is_file():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                patterns.append(line)
    return patterns


def is_ignored(rel_path: str, patterns: Iterable[str]) -> bool:
    """
    gitignore-like matching on a repo relative path:
      'dir/**'    everything below dir
      'a/*.h'     glob relative to the repo root
      '*.h'       (no slash) glob matched against the file name anywhere
    """
    rel = rel_path.replace("\\", "/")
    name = rel.rsplit("/", 1)[-1]
    for pat in patterns:
        pat = pat.replace("\\", "/").lstrip("/")
        if pat.endswith("/**"):
            base = pat[:-3]
            if rel == base or rel.startswith(base + "/") or fnmatch.fnmatch(rel, pat):
                return True
        elif "/" in pat:
            if fnmatch.fnmatch(rel, pat) or rel.startswith(pat.rstrip("/") + "/"):
                return True
        elif fnmatch.fnmatch(name, pat):
            return True
    return False


def _git(repo: Path, *args: str) -> str:
    try:
        p = subprocess.run(["git", *args], cwd=repo, capture_output=True, text=True)
    except OSError as exc:
        raise GitError(f"cannot run git: {exc}") from exc
    if p.returncode != 0:
        raise GitError(f"git {' '.join(args)}: {p.stderr.strip()}")
    return p.stdout


class GitSelection:
    """
    Which files (and which lines in them) are 'changed' for a git based run.

    mode: 'worktree' - staged + unstaged + untracked, against HEAD
          'staged'   - the index, against HEAD
          'ref'      - working tree against merge-base(REF, HEAD)
    """

    def __init__(self, repo: Path, mode: str, ref: Optional[str] = None):
        self.repo = repo
        self.mode = mode
        self.base = None
        if mode == "ref":
            try:
                self.base = _git(repo, "merge-base", ref, "HEAD").strip()
            except GitError:
                self.base = ref
        elif self._has_head():
            self.base = "HEAD"

    def _has_head(self) -> bool:
        try:
            _git(self.repo, "rev-parse", "--verify", "-q", "HEAD")
            return True
        except GitError:
            return False

    def _diff_args(self) -> List[str]:
        args = ["diff", "--no-color", "--no-ext-diff", "--diff-filter=ACMR"]
        if self.mode == "staged":
            args.append("--cached")
        if self.base:
            args.append(self.base)
        return args

    def files(self) -> List[Path]:
        names = set(_git(self.repo, *self._diff_args(), "--name-only").splitlines())
        if self.mode == "worktree":
            names |= set(_git(self.repo, "ls-files", "--others", "--exclude-standard").splitlines())
        return [self.repo / n for n in sorted(names) if n]

    def changed_lines(self, path: Path) -> Optional[Set[int]]:
        """1-based new-side line numbers changed in path; None = whole file is new."""
        try:
            Path(path).resolve().relative_to(self.repo.resolve())
        except ValueError:
            return None  # outside the repository: no history, check all of it
        rel = os.path.relpath(path, self.repo)
        if self.mode == "worktree":
            tracked = _git(self.repo, "ls-files", "--", rel).strip()
            if not tracked:
                return None
        out = _git(self.repo, *self._diff_args(), "-U0", "--", rel)
        if not out:
            # Tracked but no diff: e.g. newly added with no base commit.
            return None if not self.base else set()
        lines: Set[int] = set()
        for m in re.finditer(r"^@@ -\S+ \+(\d+)(?:,(\d+))? @@", out, re.M):
            start = int(m.group(1))
            count = int(m.group(2)) if m.group(2) is not None else 1
            lines.update(range(start, start + count))
        return lines

    def staged_content(self, path: Path) -> str:
        rel = os.path.relpath(path, self.repo)
        return _git(self.repo, "show", f":{rel}")


def collect_paths(paths: Iterable[Path], repo: Path, extensions: List[str],
                  ignore: List[str], explicit: bool) -> List[Path]:
    """
    Expand directories and filter by extension and ignore patterns. Files
    named explicitly on the command line are not subject to ignore patterns.
    """
    out: List[Path] = []
    seen: Set[Path] = set()

    def add(p: Path, check_ignore: bool):
        p = p.resolve()
        if p in seen or p.suffix not in extensions or not p.is_file():
            return
        if check_ignore:
            try:
                rel = p.relative_to(repo).as_posix()
            except ValueError:
                rel = p.as_posix()
            if is_ignored(rel, ignore):
                return
        seen.add(p)
        out.append(p)

    for p in paths:
        if p.is_dir():
            for root, dirs, files in os.walk(p):
                rel_root = Path(root).resolve()
                try:
                    rel_s = rel_root.relative_to(repo).as_posix()
                except ValueError:
                    rel_s = rel_root.as_posix()
                dirs[:] = sorted(d for d in dirs
                                 if not is_ignored(d if rel_s == "." else f"{rel_s}/{d}",
                                                   ignore))
                for f in sorted(files):
                    add(Path(root) / f, True)
        else:
            add(p, not explicit)
    return out
