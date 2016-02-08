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

static const struct ble_hs_cfg ble_hs_cfg_dflt = {
    /** Connection settings. */
    .max_outstanding_pkts_per_conn = 5,

#if NIMBLE_OPT_CONNECT
    .max_connections = 8,
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
    .max_l2cap_sig_procs = 8,
};

struct ble_hs_cfg ble_hs_cfg;

void
ble_hs_cfg_init(void)
{
    if (ble_hs_cfg.max_outstanding_pkts_per_conn == 0) {
        ble_hs_cfg.max_outstanding_pkts_per_conn =
            ble_hs_cfg_dflt.max_outstanding_pkts_per_conn;
    }

    if (ble_hs_cfg.max_connections == 0) {
        ble_hs_cfg.max_connections = ble_hs_cfg_dflt.max_connections;
    }

    if (ble_hs_cfg.max_conn_update_entries == 0) {
        ble_hs_cfg.max_conn_update_entries =
            ble_hs_cfg_dflt.max_conn_update_entries;
    }

    if (ble_hs_cfg.max_services == 0) {
        ble_hs_cfg.max_services = ble_hs_cfg_dflt.max_services;
    }

    if (ble_hs_cfg.max_client_configs == 0) {
        ble_hs_cfg.max_client_configs = ble_hs_cfg_dflt.max_client_configs;
    }

    if (ble_hs_cfg.max_gattc_procs == 0) {
        ble_hs_cfg.max_gattc_procs = ble_hs_cfg_dflt.max_gattc_procs;
    }

    if (ble_hs_cfg.max_attrs == 0) {
        ble_hs_cfg.max_attrs = ble_hs_cfg_dflt.max_attrs;
    }

    if (ble_hs_cfg.max_prep_entries == 0) {
        ble_hs_cfg.max_prep_entries = ble_hs_cfg_dflt.max_prep_entries;
    }

    if (ble_hs_cfg.max_l2cap_sig_procs == 0) {
        ble_hs_cfg.max_l2cap_sig_procs = ble_hs_cfg_dflt.max_l2cap_sig_procs;
    }
}
