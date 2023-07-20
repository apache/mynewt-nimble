/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/* btp_core.h - Bluetooth tester Core service headers */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (C) 2023 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Core Service */
#define BTP_CORE_READ_SUPPORTED_COMMANDS    0x01
struct btp_core_read_supported_commands_rp {
    uint8_t data[0];
} __packed;

#define BTP_CORE_READ_SUPPORTED_SERVICES    0x02
struct btp_core_read_supported_services_rp {
    uint8_t data[0];
} __packed;

#define BTP_CORE_REGISTER_SERVICE        0x03
struct btp_core_register_service_cmd {
    uint8_t id;
} __packed;

#define BTP_CORE_UNREGISTER_SERVICE        0x04
struct btp_core_unregister_service_cmd {
    uint8_t id;
} __packed;

/* events */
#define BTP_CORE_EV_IUT_READY        0x80