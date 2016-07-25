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

#if NIMBLE_OPT(CONNECT)
#define BLE_HS_CFG_MAX_CONNECTIONS NIMBLE_OPT(MAX_CONNECTIONS)
#else
#define BLE_HS_CFG_MAX_CONNECTIONS 0
#endif

const struct ble_hs_cfg ble_hs_cfg_dflt = {
    /** HCI settings. */
    .max_hci_bufs = 3,

    /** Connection settings. */
    .max_connections = BLE_HS_CFG_MAX_CONNECTIONS,

    /** GATT server settings. */
    /* These get set to zero with the expectation that they will be increased
     * as needed when each supported GATT service is initialized.
     */
    .max_services = 0,
    .max_client_configs = 0,

    /** GATT client settings. */
    .max_gattc_procs = 4,

    /** ATT server settings. */
    /* This is set to 0; see note above re: GATT server settings. */
    .max_attrs = 0,
    .max_prep_entries = 6,

    /** L2CAP settings. */
    /* Three channels per connection (sig, att, and sm). */
    .max_l2cap_chans = 3 * BLE_HS_CFG_MAX_CONNECTIONS,
    .max_l2cap_sig_procs = 1,
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

    /** Privacy settings. */
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
