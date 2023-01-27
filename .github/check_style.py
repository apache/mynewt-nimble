#!/usr/bin/python

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

import os.path
import re
import subprocess
import tempfile
import sys

INFO_URL = "https://github.com/apache/mynewt-core/blob/master/CODING_STANDARDS.md"

def get_lines_range(m: re.Match) -> range:
    first = int(m.group(1))

    if m.group(2) is not None:
        last = first + int(m.group(2))
    else:
        last = first + 1

    return range(first, last)


def run_cmd(cmd: str) -> list[str]:
    out = subprocess.check_output(cmd, text=True, shell=True)
    return out.splitlines()


def check_file(fname: str, commit: str, upstream: str) -> list[str]:
    ret = []

    diff_lines = set()
    for s in run_cmd(f"git diff -U0 {upstream} {commit} -- {fname}"):
        m = re.match(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", s)
        if not m:
            continue
        diff_lines.update(get_lines_range(m))

    with tempfile.NamedTemporaryFile(suffix=os.path.basename(fname)) as tmpf:
        lines = subprocess.check_output(f"git show {commit}:{fname}",
                                        shell=True)
        tmpf.write(lines)

        in_chunk = False

        for s in run_cmd(f"uncrustify -q -c uncrustify.cfg -f {tmpf.name} | "
                         f"diff -u0 -p {tmpf.name} - || true"):
            m = re.match(r"^@@ -(\d+)(?:,(\d+))? \+\d+(?:,\d+)? @@", s)
            if not m:
                if in_chunk:
                    ret.append(s)
                continue

            in_chunk = len(diff_lines & set(get_lines_range(m))) != 0

            if in_chunk:
                ret.append(s)

    return ret


def is_ignored(fname: str, ign_dirs: list[str]) -> bool:
    if not re.search(r"\.(c|cpp|h|hpp)$", fname):
        return True

    for d in ign_dirs:
        if fname.startswith(d):
            return True

    return False


def main() -> bool:
    if len(sys.argv) > 1:
        commit = sys.argv[1]
    else:
        commit = "HEAD"
    if len(sys.argv) > 2:
        upstream = sys.argv[2]
    else:
        upstream = "origin/master"

    mb = run_cmd(f"git merge-base {upstream} {commit}")
    upstream = mb[0]

    cfg_fname = os.path.join(os.path.dirname(__file__), "../.style_ignored_dirs")
    with open(cfg_fname, "r") as x:
        ign_dirs = [s.strip() for s in x.readlines() if
                    s.strip() and not s.startswith("#")]

    results_ok = []
    results_fail = []
    results_ign = []

    files = run_cmd(f"git diff --diff-filter=AM --name-only {upstream} {commit}")
    for fname in files:
        if is_ignored(fname, ign_dirs):
            results_ign.append(fname)
            continue

        diff = check_file(fname, commit, upstream)
        if len(diff) == 0:
            results_ok.append(fname)
        else:
            results_fail.append((fname, diff))

    for fname in results_ign:
        print(f"\033[90m[ NA ] {fname}\033[0m")
    for fname in results_ok:
        print(f"\033[32m[ OK ] {fname}\033[0m")
    for fname, diff in results_fail:
        print(f"\033[31m[FAIL] {fname}\033[0m")
        print("\n".join(diff))
        print()

    if len(results_fail) > 0:
        print(f"\033[31mYour code has style problems, see {INFO_URL} for "
              f"details.\033[0m")

    return len(results_fail) == 0


if __name__ == "__main__":
    if not main():
        sys.exit(1)
