/**
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

#include "ble_hs_priv.h"

const struct ble_hs_cfg ble_hs_cfg_dflt = {
    /** HCI settings. */
    .max_hci_bufs = 8,

    /** Connection settings. */
#if NIMBLE_OPT(CONNECT)
    .max_connections = NIMBLE_OPT(MAX_CONNECTIONS),
#else
    .max_connections = 0,
#endif

    /** GATT server settings. */
    .max_services = 16,
    .max_client_configs = 32,

    /** GATT client settings. */
    .max_gattc_procs = 16,

    /** ATT server settings. */
    .max_attrs = 64,
    .max_prep_entries = 6,

    /** L2CAP settings. */
    .max_l2cap_chans = 16,
    .max_l2cap_sig_procs = 8,
    .max_l2cap_sm_procs = 1,

    /** Security manager settings. */
    .sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT,
    .sm_oob_data_flag = 0,
    .sm_bonding = 0,
    .sm_mitm = 0,
    .sm_sc = 0,
    .sm_keypress = 0,
    .sm_our_key_dist = 0,
    .sm_their_key_dist = 0,
    /** privacy info */
    .rpa_timeout = 300,
};

struct ble_hs_cfg ble_hs_cfg;

void
ble_hs_cfg_init(struct ble_hs_cfg *cfg)
{
    if (cfg == NULL) {
        ble_hs_cfg = ble_hs_cfg_dflt;
    } else {
        ble_hs_cfg = *cfg;
    }
}
