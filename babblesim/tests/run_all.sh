#!/bin/bash
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

# Runs all BabbleSim tests, returns non-zero if any of them failed.
# Shall be run from newt project directory after building babblesim targets.

dir=$(dirname "$0")
rc=0

for t in "${dir}"/*.sh; do
    case "$(basename "$t")" in
    common.sh|run_all.sh)
        continue
        ;;
    esac

    "$t" || rc=1
done

exit ${rc}
