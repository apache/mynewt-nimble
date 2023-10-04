#!/bin/bash -x

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

EXIT_CODE=0

APPS=$(basename -a `ls -d repos/apache-mynewt-nimble/apps/*/`)

IGNORED_APPS="mesh_badge"

for app in ${APPS}; do
    # NOTE: do not remove the spaces around IGNORED_APPS; it's required to
    #       match against the first and last entries
    if [[ " ${IGNORED_APPS} " =~ [[:blank:]]${app}[[:blank:]] ]]; then
        echo "Skipping $app"
        continue
    fi

    echo "Testing $app"

    target="test-$app"
    newt target delete -s -f $target &> /dev/null
    newt target create -s $target
    newt target set -s $target bsp="@apache-mynewt-core/hw/bsp/nordic_pca10056"
    newt target set -s $target app="@apache-mynewt-nimble/apps/$app"

    echo "Building with app default config"
    newt build -q $target

    rc=$?
    [[ $rc -ne 0 ]] && EXIT_CODE=$rc

    echo "Building with all-enabled config"
    newt clean $target
    cp -f ../.github/test_build_apps_syscfg.yml targets/$target/syscfg.yml
    newt build -q $target

    rc=$?
    [[ $rc -ne 0 ]] && EXIT_CODE=$rc

    newt clean $target
    newt target delete -s -f $target
done

exit $EXIT_CODE
