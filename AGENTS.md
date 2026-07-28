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
#  KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
-->

# Agent guidance

This file is read by automated agents (security scanners, code analyzers,
AI assistants) operating on this repository. It points them at the
human-authored references they should consult before producing output.

## Security

Security model: [SECURITY.md](./SECURITY.md), which links to the project's
threat model at [THREAT_MODEL.md](./THREAT_MODEL.md).

Apache NimBLE is a Bluetooth Low Energy (BLE) host + controller stack.
Before reporting a security issue, consult `THREAT_MODEL.md`: most BLE
traffic is unauthenticated by the Bluetooth specification (open
advertising, pre-encryption traffic, and "Just Works" pairing has no MITM
protection), and those spec-defined behaviours are not NimBLE defects. The
in-model surface is memory safety in the radio PDU parsers (LL / HCI /
L2CAP / ATT / SM) and the correctness of the Security Manager's pairing
cryptography. See the §11a known-non-findings list.
