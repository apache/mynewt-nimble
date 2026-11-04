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

# Advertiser and scanner: scanner shall receive advertising reports in each
# discovery procedure.

. "$(dirname "$0")/common.sh"

start_device advertiser nrf52833_advertiser
start_device scanner nrf52833_scanner
run_phy 3.5e6

expect advertiser "Starting advertising"
expect scanner "name(complete)=Apache Mynewt" 3
expect scanner "Discovery completed, terminaton code: 0" 3
expect_none advertiser "Assertion failed"
expect_none scanner "Assertion failed"

finish
