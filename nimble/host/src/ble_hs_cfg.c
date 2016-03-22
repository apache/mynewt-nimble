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
    .max_hci_tx_slots = 8,

    /** Connection settings. */
    .max_outstanding_pkts_per_conn = 5,

#if NIMBLE_OPT_CONNECT
    .max_connections = NIMBLE_OPT_MAX_CONNECTIONS,
    .max_conn_update_entries = 4,
#else
    .max_connections = 0,
    .max_conn_update_entries = 0,
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
