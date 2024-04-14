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
#include "ble_ll_conn_priv.h"
#include "ble_ll_cs_priv.h"

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

void
ble_ll_cs_capabilities_pdu_make(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    const struct ble_ll_cs_supp_cap *cap = &g_ble_ll_cs_local_cap;

    *dptr = cap->mode_types;
    dptr[1] = cap->rtt_capability;
    dptr[2] = cap->rtt_aa_only_n;
    dptr[3] = cap->rtt_sounding_n;
    dptr[4] = cap->rtt_random_sequence_n;
    put_le16(dptr + 5, cap->nadm_sounding_capability);
    put_le16(dptr + 7, cap->nadm_random_sequence_capability);
    dptr[9] = cap->cs_sync_phy_capability;
    dptr[10] = cap->number_of_antennas | cap->max_number_of_antenna_paths << 4;
    dptr[11] = cap->roles_supported |
               cap->no_fae << 3 |
               cap->channel_selection << 4 |
               cap->sounding_pct_estimate << 5;
    dptr[12] = cap->max_number_of_configs;
    put_le16(dptr + 13, cap->max_number_of_procedures);
    dptr[15] = cap->t_sw;
    put_le16(dptr + 16, cap->t_ip1_capability & ~(1 << T_IP1_CAP_ID_145US));
    put_le16(dptr + 18, cap->t_ip2_capability & ~(1 << T_IP2_CAP_ID_145US));
    put_le16(dptr + 20, cap->t_fcs_capability & ~(1 << T_FCS_CAP_ID_150US));
    put_le16(dptr + 22, cap->t_pm_capability & ~(1 << T_PM_CAP_ID_40US));
    dptr[24] = cap->tx_snr_capablity << 1;
}

static void
ble_ll_cs_update_rem_capabilities(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    struct ble_ll_cs_supp_cap *cap = &connsm->cssm->remote_cap;

    cap->mode_types = *dptr & 0x01;
    cap->rtt_capability = dptr[1] & 0x05;
    cap->rtt_aa_only_n = dptr[2];
    cap->rtt_sounding_n = dptr[3];
    cap->rtt_random_sequence_n = dptr[4];
    cap->nadm_sounding_capability = get_le16(dptr + 5) & 0x01;
    cap->nadm_random_sequence_capability = get_le16(dptr + 7) & 0x01;
    cap->cs_sync_phy_capability = dptr[9] & 0x06;

    cap->number_of_antennas = dptr[10] & 0b00001111;
    cap->max_number_of_antenna_paths = dptr[10] >> 4;

    cap->roles_supported = dptr[11] & 0b00000011;
    cap->no_fae = (dptr[11] & 0b00001000) >> 3;
    cap->channel_selection = (dptr[11] & 0b00010000) >> 4;
    cap->sounding_pct_estimate = (dptr[11] & 0b00100000) >> 5;

    cap->max_number_of_configs = dptr[12];
    cap->max_number_of_procedures = get_le16(dptr + 13);
    cap->t_sw = dptr[15];
    cap->t_ip1_capability = (get_le16(dptr + 16) & 0x00FF) | (1 << T_IP1_CAP_ID_145US);
    cap->t_ip2_capability = (get_le16(dptr + 18) & 0x00FF) | (1 << T_IP1_CAP_ID_145US);
    cap->t_fcs_capability = (get_le16(dptr + 20) & 0x03FF) | (1 << T_FCS_CAP_ID_150US);
    cap->t_pm_capability = (get_le16(dptr + 22) & 0x07) | (1 << T_PM_CAP_ID_40US);
    cap->tx_snr_capablity = (dptr[24] >> 1) & 0b01111111;

    /* The capabilites contain info about allowed values for
     * CS procedures. Ignore the RFU values here.
     * We will be able to reject/renegotiate unsupported values
     * if the remote controller will use them in the procedures.
     */

    if (cap->number_of_antennas > 4) {
        cap->number_of_antennas = 4;
    }

    if (cap->max_number_of_antenna_paths > 4) {
        cap->max_number_of_antenna_paths = 4;
    }

    if (cap->max_number_of_antenna_paths < cap->number_of_antennas) {
        cap->number_of_antennas = cap->max_number_of_antenna_paths;
    }

    if (cap->max_number_of_configs > 4) {
        cap->max_number_of_configs = 4;
    }

    if (!(cap->t_sw == 0x00 ||
          cap->t_sw == 0x01 ||
          cap->t_sw == 0x02 ||
          cap->t_sw == 0x04 ||
          cap->t_sw == 0x0A)) {
        /* If the remote does not support a valid duration of the antenna switch period,
         * lets assume it does not support the antenna switching at all.
         */
        cap->number_of_antennas = 1;
        cap->t_sw = 0;
    }
}

static void
ble_ll_cs_ev_rd_rem_supp_cap(struct ble_ll_conn_sm *connsm, uint8_t status)
{
    const struct ble_ll_cs_supp_cap *cap = &connsm->cssm->remote_cap;
    struct ble_hci_ev_le_subev_cs_rd_rem_supp_cap_complete *ev;
    struct ble_hci_ev *hci_ev;

    if (ble_ll_hci_is_le_event_enabled(
            BLE_HCI_LE_SUBEV_CS_RD_REM_SUPP_CAP_COMPLETE)) {
        hci_ev = ble_transport_alloc_evt(0);
        if (hci_ev) {
            hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
            hci_ev->length = sizeof(*ev);
            ev = (void *) hci_ev->data;

            memset(ev, 0, sizeof(*ev));
            ev->subev_code = BLE_HCI_LE_SUBEV_CS_RD_REM_SUPP_CAP_COMPLETE;
            ev->status = status;
            ev->conn_handle = htole16(connsm->conn_handle);

            if (status == BLE_ERR_SUCCESS) {
                ev->num_config_supported = cap->max_number_of_configs;
                ev->max_consecutive_procedures_supported = htole16(cap->max_number_of_procedures);
                ev->num_antennas_supported = cap->number_of_antennas;
                ev->max_antenna_paths_supported = cap->max_number_of_antenna_paths;
                ev->roles_supported = cap->roles_supported;
                ev->optional_modes_supported = cap->mode_types;
                ev->rtt_capability = cap->rtt_capability;
                ev->rtt_aa_only_n = cap->rtt_aa_only_n;
                ev->rtt_sounding_n = cap->rtt_sounding_n;
                ev->rtt_random_payload_n = cap->rtt_random_sequence_n;
                ev->optional_nadm_sounding_capability = htole16(cap->nadm_sounding_capability);
                ev->optional_nadm_random_capability = htole16(cap->nadm_random_sequence_capability);
                ev->optional_cs_sync_phys_supported = cap->cs_sync_phy_capability;
                ev->optional_subfeatures_supported = htole16(cap->no_fae << 1 |
                                                            cap->channel_selection << 2 |
                                                            cap->sounding_pct_estimate << 3);
                ev->optional_t_ip1_times_supported = htole16(cap->t_ip1_capability & ~(1 << T_IP1_CAP_ID_145US));
                ev->optional_t_ip2_times_supported = htole16(cap->t_ip2_capability & ~(1 << T_IP2_CAP_ID_145US));
                ev->optional_t_fcs_times_supported = htole16(cap->t_fcs_capability & ~(1 << T_FCS_CAP_ID_150US));
                ev->optional_t_pm_times_supported = htole16(cap->t_pm_capability & ~(1 << T_PM_CAP_ID_40US));
                ev->t_sw_time_supported = cap->t_sw;
                ev->optional_tx_snr_capability = cap->tx_snr_capablity;
            }

            ble_ll_hci_event_send(hci_ev);
        }
    }
}

int
ble_ll_cs_rx_capabilities_req(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                              uint8_t *rspbuf)
{
    ble_ll_cs_update_rem_capabilities(connsm, dptr);

    ble_ll_cs_capabilities_pdu_make(connsm, rspbuf);

    return BLE_LL_CTRL_CS_CAPABILITIES_RSP;
}

void
ble_ll_cs_rx_capabilities_req_rejected(struct ble_ll_conn_sm *connsm, uint8_t ble_error)
{
    /* Stop the control procedure and send an event to the host */
    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_CAP_XCHG);
    ble_ll_cs_ev_rd_rem_supp_cap(connsm, ble_error);
}

void
ble_ll_cs_rx_capabilities_rsp(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    if (!IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CS_CAP_XCHG)) {
        /* Ignore */
        return;
    }

    ble_ll_cs_update_rem_capabilities(connsm, dptr);

    /* Stop the control procedure and send an event to the host */
    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_CAP_XCHG);
    ble_ll_cs_ev_rd_rem_supp_cap(connsm, BLE_ERR_SUCCESS);
}

int
ble_ll_cs_hci_rd_rem_supp_cap(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    const struct ble_hci_le_cs_rd_rem_supp_cap_cp *cmd = (const void *)cmdbuf;
    struct ble_ll_conn_sm *connsm;

    if (cmdlen != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* If no connection handle exit with error */
    connsm = ble_ll_conn_find_by_handle(le16toh(cmd->conn_handle));
    if (!connsm) {
        return BLE_ERR_UNK_CONN_ID;
    }

    /* If already pending exit with error */
    if (IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CS_CAP_XCHG)) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    ble_ll_ctrl_proc_start(connsm, BLE_LL_CTRL_PROC_CS_CAP_XCHG, NULL);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_cs_hci_wr_cached_rem_supp_cap(const uint8_t *cmdbuf, uint8_t cmdlen,
                                     uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_cs_wr_cached_rem_supp_cap_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_cs_wr_cached_rem_supp_cap_rp *rsp = (void *)rspbuf;
    struct ble_ll_cs_supp_cap *cap;
    struct ble_ll_conn_sm *connsm;
    uint16_t subfeatures;

    connsm = ble_ll_conn_find_by_handle(le16toh(cmd->conn_handle));
    if (!connsm) {
        return BLE_ERR_UNK_CONN_ID;
    }

    cap = &connsm->cssm->remote_cap;

    cap->max_number_of_configs = cmd->num_config_supported;
    cap->max_number_of_procedures = le16toh(cmd->max_consecutive_procedures_supported);
    cap->number_of_antennas = cmd->num_antennas_supported;
    cap->max_number_of_antenna_paths = cmd->max_antenna_paths_supported;
    cap->roles_supported = cmd->roles_supported;
    cap->mode_types = cmd->optional_modes_supported;
    cap->rtt_capability = cmd->rtt_capability;
    cap->rtt_aa_only_n = cmd->rtt_aa_only_n;
    cap->rtt_sounding_n = cmd->rtt_sounding_n;
    cap->rtt_random_sequence_n = cmd->rtt_random_payload_n;
    cap->nadm_sounding_capability = le16toh(cmd->optional_nadm_sounding_capability);
    cap->nadm_random_sequence_capability = le16toh(cmd->optional_nadm_random_capability);
    cap->cs_sync_phy_capability = cmd->optional_cs_sync_phys_supported;

    subfeatures = le16toh(cmd->optional_subfeatures_supported);
    cap->no_fae = (subfeatures >> 1) & 1;
    cap->channel_selection = (subfeatures >> 2) & 1;
    cap->sounding_pct_estimate = (subfeatures >> 3) & 1;

    cap->t_ip1_capability = le16toh(cmd->optional_t_ip1_times_supported) | (1 << T_IP1_CAP_ID_145US);
    cap->t_ip2_capability = le16toh(cmd->optional_t_ip2_times_supported) | (1 << T_IP2_CAP_ID_145US);
    cap->t_fcs_capability = le16toh(cmd->optional_t_fcs_times_supported) | (1 << T_FCS_CAP_ID_150US);
    cap->t_pm_capability = le16toh(cmd->optional_t_pm_times_supported) | (1 << T_PM_CAP_ID_40US);
    cap->t_sw = cmd->t_sw_time_supported;
    cap->tx_snr_capablity = cmd->optional_tx_snr_capability;

    rsp->conn_handle = cmd->conn_handle;
    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
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
