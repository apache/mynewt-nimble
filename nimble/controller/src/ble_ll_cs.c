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

#include <syscfg/syscfg.h>
#if MYNEWT_VAL(BLE_LL_CHANNEL_SOUNDING)
#include <stdint.h>
#include "nimble/hci_common.h"
#include "controller/ble_ll_utils.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_conn.h"
#include "controller/ble_ll_hci.h"
#include "controller/ble_ll_cs.h"
#include "controller/ble_ll_cs_priv.h"

#define T_IP1_CAP_ID_10US 0
#define T_IP1_CAP_ID_20US 1
#define T_IP1_CAP_ID_30US 2
#define T_IP1_CAP_ID_40US 3
#define T_IP1_CAP_ID_50US 4
#define T_IP1_CAP_ID_60US 5
#define T_IP1_CAP_ID_80US 6
#define T_IP1_CAP_ID_145US 7

#define T_IP2_CAP_ID_10US 0
#define T_IP2_CAP_ID_20US 1
#define T_IP2_CAP_ID_30US 2
#define T_IP2_CAP_ID_40US 3
#define T_IP2_CAP_ID_50US 4
#define T_IP2_CAP_ID_60US 5
#define T_IP2_CAP_ID_80US 6
#define T_IP2_CAP_ID_145US 7

#define T_FCS_CAP_ID_15US 0
#define T_FCS_CAP_ID_20US 1
#define T_FCS_CAP_ID_30US 2
#define T_FCS_CAP_ID_40US 3
#define T_FCS_CAP_ID_50US 4
#define T_FCS_CAP_ID_60US 5
#define T_FCS_CAP_ID_80US 6
#define T_FCS_CAP_ID_100US 7
#define T_FCS_CAP_ID_120US 8
#define T_FCS_CAP_ID_150US 9

#define T_PM_CAP_ID_10US 0
#define T_PM_CAP_ID_20US 1
#define T_PM_CAP_ID_40US 2

static struct ble_ll_cs_supp_cap g_ble_ll_cs_local_cap;
static struct ble_ll_cs_sm g_ble_ll_cs_sm[MYNEWT_VAL(BLE_MAX_CONNECTIONS)];

int
ble_ll_cs_hci_rd_loc_supp_cap(uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_ll_cs_supp_cap *cap = &g_ble_ll_cs_local_cap;
    struct ble_hci_le_cs_rd_loc_supp_cap_rp *rsp = (void *)rspbuf;

    rsp->num_config_supported = cap->max_number_of_configs;
    rsp->max_consecutive_procedures_supported = htole16(cap->max_number_of_procedures);
    rsp->num_antennas_supported = cap->number_of_antennas;
    rsp->max_antenna_paths_supported = cap->max_number_of_antenna_paths;
    rsp->roles_supported = cap->roles_supported;
    rsp->optional_modes_supported = cap->mode_types;
    rsp->rtt_capability = cap->rtt_capability;
    rsp->rtt_aa_only_n = cap->rtt_aa_only_n;
    rsp->rtt_sounding_n = cap->rtt_sounding_n;
    rsp->rtt_random_payload_n = cap->rtt_random_sequence_n;
    rsp->optional_nadm_sounding_capability = htole16(cap->nadm_sounding_capability);
    rsp->optional_nadm_random_capability = htole16(cap->nadm_random_sequence_capability);
    rsp->optional_cs_sync_phys_supported = cap->cs_sync_phy_capability;
    rsp->optional_subfeatures_supported = htole16(0x000f &
                                                  (cap->no_fae << 1 |
                                                   cap->channel_selection << 2 |
                                                   cap->sounding_pct_estimate << 3));
    rsp->optional_t_ip1_times_supported = htole16(cap->t_ip1_capability & ~(1 << T_IP1_CAP_ID_145US));
    rsp->optional_t_ip2_times_supported = htole16(cap->t_ip2_capability & ~(1 << T_IP2_CAP_ID_145US));
    rsp->optional_t_fcs_times_supported = htole16(cap->t_fcs_capability & ~(1 << T_FCS_CAP_ID_150US));
    rsp->optional_t_pm_times_supported = htole16(cap->t_pm_capability & ~(1 << T_PM_CAP_ID_40US));
    rsp->t_sw_time_supported = cap->t_sw;
    rsp->optional_tx_snr_capability = cap->tx_snr_capablity;

    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_cs_hci_rd_rem_supp_cap(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_wr_cached_rem_supp_cap(const uint8_t *cmdbuf, uint8_t cmdlen,
                                     uint8_t *rspbuf, uint8_t *rsplen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_sec_enable(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_set_def_settings(const uint8_t *cmdbuf, uint8_t cmdlen,
                               uint8_t *rspbuf, uint8_t *rsplen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_rd_rem_fae(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_wr_cached_rem_fae(const uint8_t *cmdbuf, uint8_t cmdlen,
                                uint8_t *rspbuf, uint8_t *rsplen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_create_config(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_remove_config(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_set_chan_class(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_set_proc_params(const uint8_t *cmdbuf, uint8_t cmdlen,
                              uint8_t *rspbuf, uint8_t *rsplen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_proc_enable(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_test(const uint8_t *cmdbuf, uint8_t cmdlen,
                   uint8_t *rspbuf, uint8_t *rsplen)
{
    return BLE_ERR_UNSUPPORTED;
}

int
ble_ll_cs_hci_test_end(void)
{
    return BLE_ERR_UNSUPPORTED;
}

void
ble_ll_cs_init(void)
{
    struct ble_ll_cs_supp_cap *cap = &g_ble_ll_cs_local_cap;

    /* Set local CS capabilities. Only basic features supported for now. */
    cap->mode_types = 0x00;
    cap->rtt_capability = 0x00;
    cap->rtt_aa_only_n = 0x00;
    cap->rtt_sounding_n = 0x00;
    cap->rtt_random_sequence_n = 0x00;
    cap->nadm_sounding_capability = 0x0000;
    cap->nadm_random_sequence_capability = 0x0000;
    cap->cs_sync_phy_capability = 0x00;
    cap->number_of_antennas = 0x01;
    cap->max_number_of_antenna_paths = 0x01;
    cap->roles_supported = 0x03;
    cap->no_fae = 0x00;
    cap->channel_selection = 0x00;
    cap->sounding_pct_estimate = 0x00;
    cap->max_number_of_configs = 0x04;
    cap->max_number_of_procedures = 0x0001;
    cap->t_sw = 0x0A;
    cap->t_ip1_capability = 1 << T_IP1_CAP_ID_145US;
    cap->t_ip2_capability = 1 << T_IP2_CAP_ID_145US;
    cap->t_fcs_capability = 1 << T_FCS_CAP_ID_150US;
    cap->t_pm_capability = 1 << T_PM_CAP_ID_40US;
    cap->tx_snr_capablity = 0x00;
}

void
ble_ll_cs_reset(void)
{
    ble_ll_cs_init();
}

void
ble_ll_cs_sm_init(struct ble_ll_conn_sm *connsm)
{
    uint8_t i;

    for (i = 0; i < ARRAY_SIZE(g_ble_ll_cs_sm); i++) {
        if (g_ble_ll_cs_sm[i].connsm == NULL) {
            connsm->cssm = &g_ble_ll_cs_sm[i];
            memset(connsm->cssm, 0, sizeof(*connsm->cssm));
            connsm->cssm->connsm = connsm;
            break;
        }
    }
}

void
ble_ll_cs_sm_free(struct ble_ll_conn_sm *connsm)
{
    if (connsm->cssm) {
        memset(connsm->cssm, 0, sizeof(*connsm->cssm));
        connsm->cssm = NULL;
    }
}

#endif /* BLE_LL_CHANNEL_SOUNDING */
