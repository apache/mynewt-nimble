#!/bin/bash

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

# Recreate working trees of project repositories restored from cache or
# artifact. Only .git directories are stored there, since working trees
# can be checked out from those and are bigger after compression.
# Submodules initialized when repository was cloned are checked out
# from repositories stored in .git/modules.
#
# Usage: checkout_repos.sh <project directory>

set -e

cd "$1"

for d in repos/*/; do
    [ -d "$d.git" ] || continue

    # .git may come from other system, use settings of this one
    opts=()
    if [ "$RUNNER_OS" = "Windows" ]; then
        opts=(-c core.symlinks=false -c core.filemode=false)
        git -C "$d" config core.symlinks false
        git -C "$d" config core.filemode false
    fi

    # Index contains file stats from system that created it
    rm -f "$d.git/index"
    git -C "$d" reset -q --hard

    if [ -f "$d.gitmodules" ]; then
        git "${opts[@]}" -C "$d" submodule update -q --recursive
        if [ "$RUNNER_OS" = "Windows" ]; then
            git -C "$d" submodule foreach -q --recursive \
                "git config core.symlinks false; git config core.filemode false"
        fi
    fi
done
