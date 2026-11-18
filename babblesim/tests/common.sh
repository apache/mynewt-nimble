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

# Common helpers for BabbleSim tests.
#
# Tests are run from newt project directory (where targets were built) and
# require BSIM_OUT_PATH to point to BabbleSim installation (for 2G4 phy).

set -u

TEST_NAME=$(basename "$0" .sh)
SIM_ID="${TEST_NAME}_$$"
LOG_DIR="${BSIM_TEST_LOG_DIR:-bin/bsim_tests}/${TEST_NAME}"
WALL_TIMEOUT="${BSIM_TEST_TIMEOUT:-120}"

DEVICES=()
DEVICE_PIDS=()
FAILED=0

if [ -z "${BSIM_OUT_PATH:-}" ]; then
    echo "BSIM_OUT_PATH is not set" >&2
    exit 1
fi

mkdir -p "${LOG_DIR:?}"
rm -f "${LOG_DIR:?}"/*.log

# target_elf <target name>
# Prints path to application binary of target built from babblesim/targets
target_elf() {
    local dir="bin/@apache-mynewt-nimble/babblesim/targets/$1"
    local elf

    elf=$(find "${dir}" -name '*.elf' 2>/dev/null | head -1)
    if [ -z "${elf}" ]; then
        echo "No binary found for target $1 (not built?)" >&2
        exit 1
    fi
    echo "${elf}"
}

# start_device <name> <target name> [extra args...]
# Starts device in background, its output is stored in ${LOG_DIR}/<name>.log
start_device() {
    local name=$1
    local elf

    elf=$(target_elf "$2") || exit 1
    shift 2

    timeout -k 5 "${WALL_TIMEOUT}" "${elf}" -s="${SIM_ID}" -p=phy \
        -d=${#DEVICES[@]} "$@" > "${LOG_DIR}/${name}.log" 2>&1 &

    DEVICES+=("${name}")
    DEVICE_PIDS+=($!)
}

# run_phy <simulation length in us>
# Runs phy for all started devices and waits for devices to finish
run_phy() {
    local rc=0
    local i

    (cd "${BSIM_OUT_PATH}/bin" && \
        timeout -k 5 "${WALL_TIMEOUT}" ./bs_2G4_phy_v1 -s="${SIM_ID}" \
            -p_id=phy -D=${#DEVICES[@]} -sim_length="$1") \
        > "${LOG_DIR}/phy.log" 2>&1 || rc=$?

    if [ ${rc} -ne 0 ]; then
        echo "FAIL: phy exited with ${rc}"
        FAILED=1
    fi

    for i in "${!DEVICES[@]}"; do
        rc=0
        wait "${DEVICE_PIDS[$i]}" || rc=$?
        if [ ${rc} -ne 0 ]; then
            echo "FAIL: ${DEVICES[$i]} exited with ${rc}"
            FAILED=1
        fi
    done
}

# expect <name> <pattern> [min count]
# Checks that output of device contains pattern at least min count times
expect() {
    local count

    if [ ! -f "${LOG_DIR}/$1.log" ]; then
        echo "FAIL: $1: no output"
        FAILED=1
        return
    fi

    count=$(grep -c -F -- "$2" "${LOG_DIR}/$1.log")
    if [ "${count:-0}" -lt "${3:-1}" ]; then
        echo "FAIL: $1: expected '$2' at least ${3:-1} time(s), found ${count}"
        FAILED=1
    fi
}

# expect_none <name> <pattern>
# Checks that output of device does not contain pattern
expect_none() {
    if [ ! -f "${LOG_DIR}/$1.log" ]; then
        echo "FAIL: $1: no output"
        FAILED=1
        return
    fi

    if grep -q -F -- "$2" "${LOG_DIR}/$1.log"; then
        echo "FAIL: $1: unexpected '$2'"
        FAILED=1
    fi
}

# finish
# Prints result and exits with proper status
finish() {
    local name

    if [ ${FAILED} -ne 0 ]; then
        for name in "${DEVICES[@]}"; do
            echo "--- ${name} output (last 20 lines):"
            tail -n 20 "${LOG_DIR}/${name}.log"
        done
        echo "${TEST_NAME}: FAILED"
        exit 1
    fi

    echo "${TEST_NAME}: PASSED"
    exit 0
}
