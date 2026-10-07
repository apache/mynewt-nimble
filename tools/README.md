<!--
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
-->

# Apache NimBLE Tools

This directory contains developer utilities and helper scripts for Apache NimBLE:

- **`mynewt_format.py`**: coding style checker and fixer, shared with
  apache-mynewt-core. Configured by [`newt-coding-rules`](../newt-coding-rules);
  files to skip are listed in [`newt-coding-rules-ignore`](../newt-coding-rules-ignore).
- **`mynewt_style/`**: the engine, its rules (`mynewt_style/rules/`, see the
  [rules README](mynewt_style/rules/README.md)) and unit tests.
- **`hci_throughput/`**: tools for BLE HCI throughput testing.

`mynewt_format.py` and `mynewt_style/` are maintained in
[apache-mynewt-core](https://github.com/apache/mynewt-core) and copied here by
the daily "Sync mynewt_format" workflow; change them there. The configuration
files are NimBLE's own.

## mynewt_format.py

Requires Python 3.11+ and nothing else. Unlike a whole-file formatter it only
touches code that breaks a rule, so hand-made line breaks and aligned blocks
stay as they are.

```bash
python3 tools/mynewt_format.py                 # check what you changed (working tree)
python3 tools/mynewt_format.py -d               # show proposed fixes as a diff
python3 tools/mynewt_format.py -i               # review each fix: y/n/a/s/A/q
python3 tools/mynewt_format.py -f               # apply all fixes
python3 tools/mynewt_format.py --staged         # check what is staged for commit
python3 tools/mynewt_format.py --git-diff origin/master
                                                # check what changed on this branch (CI)
python3 tools/mynewt_format.py path/to/file.c   # changed lines of given files/dirs
python3 tools/mynewt_format.py --whole-files path/to/file.c
                                                # every line of given files/dirs
python3 tools/mynewt_format.py --all            # every line of the whole repository
python3 tools/mynewt_format.py --report list    # one line per problem (for editors, scripts)
python3 tools/mynewt_format.py --list-rules     # every rule, -v for full details
python3 tools/mynewt_format.py --explain define-alignment
python3 tools/mynewt_format.py --install-hook   # pre-commit check of staged lines
```

By default only lines changed according to git are reported and fixed (new
files completely), so older code that predates a rule does not get in the
way; `--whole-files` or `--all` check everything. Each problem is shown with
its source line and, when it can be fixed, the proposed fix right below it;
`--report list` prints one `file:line:col` line per problem instead. CI runs it the same way on
every pull request as an informational check: it prints problems and proposed
fixes but does not fail the build (clang-format remains the blocking style
check for now). To silence a rule locally, use a comment:

```c
x = foo;       /* mynewt-format: ignore line-length */
/* mynewt-format: ignore-next-line */
/* mynewt-format: off */  ...  /* mynewt-format: on */
```

Existing `/* clang-format off */` ... `/* clang-format on */` regions are
honoured too.

The exit status is 0 when clean, 1 when problems were found (or `-d` proposes
changes), and 2 on usage or configuration errors. `--format github` prints
GitHub Actions annotations.

Tests: `python3 -m unittest discover -s tools/mynewt_style/tests`
