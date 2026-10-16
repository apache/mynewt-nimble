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

# Central and peripheral: central shall connect, discover services, read,
# write and subscribe to peripheral characteristics.

. "$(dirname "$0")/common.sh"

start_device blecent nrf52833_blecent
start_device bleprph nrf52833_bleprph
run_phy 5e6

expect blecent "Connection established"
expect blecent "Service discovery complete; status=0"
expect bleprph "connection established; status=0"
expect bleprph "subscribe event; conn_handle=1 attr_handle=23 reason=1"
expect_none blecent "Assertion failed"
expect_none bleprph "Assertion failed"
expect_none blecent "Error: "

finish
