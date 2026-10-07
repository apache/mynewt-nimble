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

"""Interactive review of proposed fixes, one hunk at a time (like 'git add -p')."""

from __future__ import annotations

from pathlib import Path
from typing import Callable, List, Optional

from .engine import FileResult, Hunk, apply_hunks
from .report import Colors, colorize_diff_line, display_path

HELP = """\
y - apply this change
n - skip this change
a - apply this change and all remaining changes in this file
s - skip this change and all remaining changes in this file
A - apply this change and every remaining change in all files
q - quit; changes already accepted in this file are still written
? - show this help"""

CONTEXT = 3


class QuitReview(Exception):
    """Raised when the user answers 'q'. Carries the content to write, if any."""

    def __init__(self, content: Optional[str]):
        super().__init__("quit")
        self.content = content


class Reviewer:
    def __init__(self, root: Optional[Path], ask: Callable[[str], str] = input,
                 say: Callable[[str], None] = print):
        self.root = root
        self.ask = ask
        self.say = say
        self.accept_everything = False

    def _show(self, result: FileResult, hunk: Hunk, index: int, total: int) -> None:
        a = result.original.splitlines(keepends=True)
        path = display_path(result.path, self.root)
        self.say("")
        self.say(f"{Colors.BOLD}{path}:{hunk.a_start + 1}{Colors.RESET}  "
                 f"{Colors.DIM}(change {index}/{total}){Colors.RESET}")
        for v in hunk.violations:
            self.say(f"  {Colors.CYAN}{v.rule}{Colors.RESET}: {v.message}")
        if not hunk.violations:
            self.say(f"  {Colors.DIM}(follow-up formatting of a nearby fix){Colors.RESET}")
        start = max(0, hunk.a_start - CONTEXT)
        end = min(len(a), hunk.a_end + CONTEXT)
        self.say(colorize_diff_line(
            f"@@ -{start + 1},{end - start} "
            f"+{start + 1},{end - start - (hunk.a_end - hunk.a_start) + len(hunk.new_lines)} @@"))
        for line in a[start:hunk.a_start]:
            self.say(" " + line.rstrip("\r\n"))
        for line in a[hunk.a_start:hunk.a_end]:
            self.say(colorize_diff_line("-" + line.rstrip("\r\n")))
        for line in hunk.new_lines:
            self.say(colorize_diff_line("+" + line.rstrip("\r\n")))
        for line in a[hunk.a_end:end]:
            self.say(" " + line.rstrip("\r\n"))

    def review(self, result: FileResult) -> Optional[str]:
        """
        Ask about every hunk of result. Returns the new content to write, or
        None if nothing was accepted. Raises QuitReview on 'q'.
        """
        accepted: List[Hunk] = []
        hunks = result.hunks
        take_rest = self.accept_everything
        for i, hunk in enumerate(hunks, 1):
            if take_rest:
                accepted.append(hunk)
                continue
            self._show(result, hunk, i, len(hunks))
            while True:
                try:
                    answer = self.ask(f"{Colors.BOLD}Apply this change [y,n,a,s,A,q,?]? "
                                      f"{Colors.RESET}").strip()
                except EOFError:
                    answer = "q"
                if answer in ("y", "n", "a", "s", "A", "q"):
                    break
                self.say(HELP)
            if answer == "y":
                accepted.append(hunk)
            elif answer == "a":
                accepted.append(hunk)
                take_rest = True
            elif answer == "A":
                accepted.append(hunk)
                take_rest = True
                self.accept_everything = True
            elif answer == "s":
                break
            elif answer == "q":
                raise QuitReview(apply_hunks(result.original, accepted) if accepted else None)
        if not accepted:
            return None
        return apply_hunks(result.original, accepted)
