#!/usr/bin/env python3

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

import os
import re
import subprocess
import sys
from pathlib import Path

TMP_FOLDER = "/tmp/doxygen_check"
WARN_FILE_NAME = "warnings.log"
allowed_files = [
    "nimble/include/nimble/ble.h",
]


def run_cmd(cmd: str) -> list[str]:
    out = subprocess.check_output(cmd, text=True, shell=True)
    return out.splitlines()


def run_cmd_no_check(cmd: str) -> list[str]:
    out = subprocess.run(cmd, text=True, shell=True, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL).stdout
    return out.splitlines()


def is_ignored(filename: str, allowed: list[str]) -> bool:
    if Path(filename).suffix != ".h" or "priv" in filename:
        return True
    if filename in allowed:
        return False
    if re.search(r"nimble/host.*include.*", filename):
        return False

    return True


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

    results_ok = []
    results_fail = []
    results_ign = []

    os.makedirs(Path(TMP_FOLDER, WARN_FILE_NAME).parent, mode=0o755,
                exist_ok=True)

    files = run_cmd(f"git diff --diff-filter=AM --name-only {upstream} {commit}")
    for fname in files:
        if is_ignored(fname, allowed_files):
            results_ign.append(fname)
            continue

        os.environ['DOXYGEN_INPUT'] = fname
        try:
            run_cmd_no_check("doxygen")

        except subprocess.CalledProcessError as e:
            print(f"\033[31m[FAIL] {e.returncode}\033[0m")
            return False

        with open(os.path.join(TMP_FOLDER, WARN_FILE_NAME)) as warn_log:
            warnings = warn_log.read()
            if len(warnings) == 0:
                results_ok.append(fname)
            else:
                results_fail.append((fname, warnings))

    for fname in results_ign:
        print(f"\033[90m[ NA ] {fname}\033[0m")
    for fname in results_ok:
        print(f"\033[32m[ OK ] {fname}\033[0m")
    for fname, warnings in results_fail:
        print(f"\033[31m[FAIL] {fname}\033[0m")
        print(warnings)

    if len(results_fail) > 0:
        print(f"\033[31mYour code has documentation style problems, see the logs "
              f"for details.\033[0m")

    return len(results_fail) == 0


if __name__ == "__main__":
    if not main():
        sys.exit(1)
