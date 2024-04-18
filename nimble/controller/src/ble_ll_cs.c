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
#include "controller/ble_ll_tmr.h"
#include "ble_ll_conn_priv.h"
#include "ble_ll_cs_priv.h"
#include "os/os_mbuf.h"

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

struct ble_ll_cs_supp_cap g_ble_ll_cs_local_cap;
struct ble_ll_cs_sm g_ble_ll_cs_sm[MYNEWT_VAL(BLE_MAX_CONNECTIONS)];
static const uint8_t t_ip1[] = {10, 20, 30, 40, 50, 60, 80, 145};
static const uint8_t t_ip2[] = {10, 20, 30, 40, 50, 60, 80, 145};
static const uint8_t t_fcs[] = {15, 20, 30, 40, 50, 60, 80, 100, 120, 150};
static const uint8_t t_pm[] = {10, 20, 40};
static const uint8_t default_channel_classification[10] = {
    0xFC, 0xFF, 0x7F, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F
};
static uint8_t g_ble_ll_cs_chan_class[10];

#define MIN_CONN_EVENT_COUNT_BEFORE_START 8
#define OFFSET_FROM_CONN_EVENT_TICKS 10

extern int8_t g_ble_ll_tx_power;
void ble_ll_ctrl_rej_ext_ind_make(uint8_t rej_opcode, uint8_t err, uint8_t *ctrdata);

#define div_ceil(a, b) (((a) + (b) - 1) / (b))

static int
ble_ll_cs_verify_chan_map(const uint8_t *chan_map)
{
    uint8_t i, j, byte;

    if (chan_map[0] & 0b00000011 ||
        chan_map[2] & 0b10000000 ||
        chan_map[3] & 0b00000011 ||
        chan_map[9] & 0b11100000) {
        /* Channels 0, 1, 23, 24, 25, 77, 78, and the bit 79 (non-channel)
         * are RFU. At least 15 channels shall be enabled.
         */
        return BLE_ERR_CHAN_CLASS;
    }

    for (i = 0, j = 0; i < 10; ++i) {
        byte = chan_map[i];

        while (byte) {
            if (byte & 1) {
                j++;
            }
            byte >>= 1;
        }
    }

    if (j < 15) {
        return BLE_ERR_INSUFFICIENT_CHAN;
    }

    return 0;
}

static int
ble_ll_cs_proc_params_channels_setup(struct ble_ll_cs_config *config)
{
    uint8_t i, j, byte, next_id;

    for (i = 0, j = 0; i < 10; ++i) {
        byte = config->chan_map[i] & g_ble_ll_cs_chan_class[i];
        next_id = i * 8;

        while (byte) {
            if (byte & 1) {
                config->filtered_channels[j++] = next_id;
            }
            ++next_id;
            byte >>= 1;
        }
    }

    if (j < 15) {
        config->filtered_channels_count = 0;
        return BLE_ERR_INSUFFICIENT_CHAN;
    }

    config->filtered_channels_count = j;

    return 0;
}

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
    rsp->optional_t_ip1_times_supported = htole16(cap->t_ip1_capability);
    rsp->optional_t_ip2_times_supported = htole16(cap->t_ip2_capability);
    rsp->optional_t_fcs_times_supported = htole16(cap->t_fcs_capability);
    rsp->optional_t_pm_times_supported = htole16(cap->t_pm_capability);
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
    put_le16(dptr + 16, cap->t_ip1_capability);
    put_le16(dptr + 18, cap->t_ip2_capability);
    put_le16(dptr + 20, cap->t_fcs_capability);
    put_le16(dptr + 22, cap->t_pm_capability);
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
    cap->t_ip1_capability = get_le16(dptr + 16);
    cap->t_ip2_capability = get_le16(dptr + 18);
    cap->t_fcs_capability = get_le16(dptr + 20);
    cap->t_pm_capability = get_le16(dptr + 22);
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
                ev->optional_t_ip1_times_supported = htole16(cap->t_ip1_capability);
                ev->optional_t_ip2_times_supported = htole16(cap->t_ip2_capability);
                ev->optional_t_fcs_times_supported = htole16(cap->t_fcs_capability);
                ev->optional_t_pm_times_supported = htole16(cap->t_pm_capability);
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

    ble_ll_ctrl_proc_start(connsm, BLE_LL_CTRL_PROC_CS_CAP_XCHG);

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

    cap->t_ip1_capability = le16toh(cmd->optional_t_ip1_times_supported);
    cap->t_ip2_capability = le16toh(cmd->optional_t_ip2_times_supported);
    cap->t_fcs_capability = le16toh(cmd->optional_t_fcs_times_supported);
    cap->t_pm_capability = le16toh(cmd->optional_t_pm_times_supported);
    cap->t_sw = cmd->t_sw_time_supported;
    cap->tx_snr_capablity = cmd->optional_tx_snr_capability;

    rsp->conn_handle = cmd->conn_handle;
    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

static void
ble_ll_cs_ev_sec_enable_complete(struct ble_ll_conn_sm *connsm, uint8_t status)
{
    struct ble_hci_ev_le_subev_cs_sec_enable_complete *ev;
    struct ble_hci_ev *hci_ev;

    if (ble_ll_hci_is_le_event_enabled(
            BLE_HCI_LE_SUBEV_CS_SEC_ENABLE_COMPLETE)) {
        hci_ev = ble_transport_alloc_evt(0);
        if (hci_ev) {
            hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
            hci_ev->length = sizeof(*ev);
            ev = (void *) hci_ev->data;

            ev->subev_code = BLE_HCI_LE_SUBEV_CS_SEC_ENABLE_COMPLETE;
            ev->status = status;
            ev->conn_handle = htole16(connsm->conn_handle);

            ble_ll_hci_event_send(hci_ev);
        }
    }
}

int
ble_ll_cs_rx_security_req(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                          uint8_t *rspbuf)
{
    uint8_t *iv = connsm->cssm->drbg_ctx.iv;
    uint8_t *in = connsm->cssm->drbg_ctx.in;
    uint8_t *pv = connsm->cssm->drbg_ctx.pv;

    if (!connsm->flags.encrypted) {
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_SEC_REQ,
                                     BLE_ERR_INSUFFICIENT_SEC, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    /* Vectors concatenation is done in the follwing manner:
     * CS_IV = CS_IV_P || CS_IV_C
     * The CS_IV_C is concatenated with the CS_IV_P. The least significant
     * octet of CS_IV_C becomes the least significant octet of CS_IV. The most
     * significant octet of CS_IV_P becomes the most significant octet of CS_IV.
     */

    /* Save Central's vector */
    memcpy(iv, dptr, 8);
    memcpy(in, dptr + 8, 4);
    memcpy(pv, dptr + 12, 8);

    /* Generate Peripheral's vector */
    ble_ll_rand_data_get(iv + 8, 8);
    ble_ll_rand_data_get(in + 4, 4);
    ble_ll_rand_data_get(pv + 8, 8);

    memcpy(rspbuf, iv + 8, 8);
    memcpy(rspbuf + 8, in + 4, 4);
    memcpy(rspbuf + 12, pv + 8, 8);

    ble_ll_cs_drbg_init(&connsm->cssm->drbg_ctx);

    connsm->cssm->next_procedure_id = 0;

    ble_ll_cs_ev_sec_enable_complete(connsm, BLE_ERR_SUCCESS);

    return BLE_LL_CTRL_CS_SEC_RSP;
}

void
ble_ll_cs_rx_security_rsp(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    int rc = 0;
    struct ble_ll_cs_drbg_ctx *drbg_ctx = &connsm->cssm->drbg_ctx;

    if (!IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CS_SEC_START)) {
        /* Ignore */
        return;
    }

    /* Save Peripheral's vector */
    memcpy(drbg_ctx->iv + 8, dptr, 8);
    memcpy(drbg_ctx->in + 4, dptr + 8, 4);
    memcpy(drbg_ctx->pv + 8, dptr + 12, 8);

    rc = ble_ll_cs_drbg_init(drbg_ctx);

    connsm->cssm->next_procedure_id = 0;

    /* Stop the control procedure and send an event to the host */
    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_SEC_START);
    ble_ll_cs_ev_sec_enable_complete(connsm, rc ? BLE_ERR_INV_LMP_LL_PARM : BLE_ERR_SUCCESS);
}

void
ble_ll_cs_rx_security_req_rejected(struct ble_ll_conn_sm *connsm, uint8_t ble_error)
{
    /* Stop the control procedure and send an event to the host */
    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_SEC_START);
    ble_ll_cs_ev_sec_enable_complete(connsm, ble_error);
}

void
ble_ll_cs_security_req_make(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    uint8_t *iv = connsm->cssm->drbg_ctx.iv;
    uint8_t *in = connsm->cssm->drbg_ctx.in;
    uint8_t *pv = connsm->cssm->drbg_ctx.pv;

    /* Generate Central's vector */
    ble_ll_rand_data_get(iv, 8);
    ble_ll_rand_data_get(in, 4);
    ble_ll_rand_data_get(pv, 8);

    memcpy(dptr, iv, 8);
    memcpy(dptr + 8, in, 4);
    memcpy(dptr + 12, pv, 8);
}

int
ble_ll_cs_hci_sec_enable(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    const struct ble_hci_le_cs_sec_enable_cp *cmd = (const void *)cmdbuf;
    struct ble_ll_conn_sm *connsm;

    connsm = ble_ll_conn_find_by_handle(le16toh(cmd->conn_handle));
    if (!connsm) {
        return BLE_ERR_UNK_CONN_ID;
    }

    if (!connsm->flags.encrypted) {
        return BLE_ERR_INSUFFICIENT_SEC;
    }

    ble_ll_ctrl_proc_start(connsm, BLE_LL_CTRL_PROC_CS_SEC_START);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_cs_hci_set_def_settings(const uint8_t *cmdbuf, uint8_t cmdlen,
                               uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_cs_set_def_settings_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_cs_set_def_settings_rp *rsp = (void *)rspbuf;
    const struct ble_ll_cs_supp_cap *cap = &g_ble_ll_cs_local_cap;
    struct ble_ll_conn_sm *connsm;
    struct ble_ll_cs_sm *cssm;
    uint8_t i;

    connsm = ble_ll_conn_find_by_handle(le16toh(cmd->conn_handle));
    if (!connsm) {
        return BLE_ERR_UNK_CONN_ID;
    }

    cssm = connsm->cssm;

    /* Check if a disabled role is used in CS configs */
    for (i = 0; i < ARRAY_SIZE(cssm->config); i++) {
        struct ble_ll_cs_config *conf = &cssm->config[i];

        if (conf->config_enabled && (1 << conf->role) & ~cmd->role_enable) {
            return BLE_ERR_INV_HCI_CMD_PARMS;
        }
    }

    if ((cmd->role_enable & ~cap->roles_supported) != 0 ||
        (cap->number_of_antennas < cmd->cs_sync_antenna_selection &&
         cmd->cs_sync_antenna_selection < 0xFE)) {
        /* Unsupported role or antenna selection used */
        return BLE_ERR_UNSUPPORTED;
    }

    /* Allowed Transmit_Power_Level range: -127 to +20,
     * (Complement system + special meaning for 0x7E and 0x7F)
     */
    if (!(IN_RANGE(cmd->max_tx_power, 0x00, 0x14) ||
          IN_RANGE(cmd->max_tx_power, 0x7E, 0xFF))) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (cmd->max_tx_power == 0x7E) {
        /* TODO: Set transmitter to minimum transmit power level
         * supported by the board.
         */
        cssm->max_tx_power = 0x80;
    } else if (cmd->max_tx_power == 0x7F) {
        /* TODO: Set transmitter to maximum transmit power level
         * supported by the board.
         */
        cssm->max_tx_power = 0x14;
    } else {
        /* TODO: Set transmitter to the nearest transmit power level
         * supported by the board.
         */
        cssm->max_tx_power = cmd->max_tx_power;
    }

    cssm->roles_enabled = cmd->role_enable;
    cssm->cs_sync_antenna_selection = cmd->cs_sync_antenna_selection;

    rsp->conn_handle = cmd->conn_handle;
    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

static void
ble_ll_cs_ev_rd_rem_fae_complete(struct ble_ll_conn_sm *connsm, uint8_t status)
{
    struct ble_hci_ev_le_subev_cs_rd_rem_fae_complete *ev;
    struct ble_hci_ev *hci_ev;

    if (ble_ll_hci_is_le_event_enabled(
            BLE_HCI_LE_SUBEV_CS_RD_REM_FAE_COMPLETE)) {
        hci_ev = ble_transport_alloc_evt(0);
        if (hci_ev) {
            hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
            hci_ev->length = sizeof(*ev);
            ev = (void *) hci_ev->data;

            memset(ev, 0, sizeof(*ev));
            ev->subev_code = BLE_HCI_LE_SUBEV_CS_RD_REM_FAE_COMPLETE;
            ev->status = status;
            ev->conn_handle = htole16(connsm->conn_handle);

            if (status == BLE_ERR_SUCCESS) {
                memcpy(ev->remote_fae_table, connsm->cssm->remote_fae_table, 72);
            }

            ble_ll_hci_event_send(hci_ev);
        }
    }
}

int
ble_ll_cs_rx_fae_req(struct ble_ll_conn_sm *connsm, struct os_mbuf *om)
{
    /* Space for response code */
    om->om_len = 1;
    OS_MBUF_PKTLEN(om) = om->om_len;
    os_mbuf_append(om, connsm->cssm->local_fae_table, 72);

    ble_ll_cs_ev_rd_rem_fae_complete(connsm, BLE_ERR_SUCCESS);

    return BLE_LL_CTRL_CS_FAE_RSP;
}

void
ble_ll_cs_rx_fae_rsp(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    if (!IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CS_FAE_REQ)) {
        /* Ignore */
        return;
    }

    memcpy(connsm->cssm->remote_fae_table, dptr, 72);

    /* Stop the control procedure and send an event to the host */
    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_FAE_REQ);
    ble_ll_cs_ev_rd_rem_fae_complete(connsm, BLE_ERR_SUCCESS);
}

void
ble_ll_cs_rx_fae_req_rejected(struct ble_ll_conn_sm *connsm, uint8_t ble_error)
{
    /* Stop the control procedure and send an event to the host */
    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_FAE_REQ);
    ble_ll_cs_ev_rd_rem_fae_complete(connsm, ble_error);
}

int
ble_ll_cs_hci_rd_rem_fae(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    const struct ble_hci_le_cs_rd_rem_fae_cp *cmd = (const void *)cmdbuf;
    struct ble_ll_conn_sm *connsm;

    connsm = ble_ll_conn_find_by_handle(le16toh(cmd->conn_handle));
    if (!connsm) {
        return BLE_ERR_UNK_CONN_ID;
    }

    ble_ll_ctrl_proc_start(connsm, BLE_LL_CTRL_PROC_CS_FAE_REQ);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_cs_hci_wr_cached_rem_fae(const uint8_t *cmdbuf, uint8_t cmdlen,
                                uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_cs_wr_cached_rem_fae_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_cs_wr_cached_rem_fae_rp *rsp = (void *)rspbuf;
    struct ble_ll_conn_sm *connsm;

    connsm = ble_ll_conn_find_by_handle(le16toh(cmd->conn_handle));
    if (!connsm) {
        return BLE_ERR_UNK_CONN_ID;
    }

    memcpy(connsm->cssm->remote_fae_table, cmd->remote_fae_table, 72);

    rsp->conn_handle = cmd->conn_handle;
    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

void
ble_ll_cs_config_req_make(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    uint8_t config_id = connsm->cssm->config_req_id;
    uint8_t action = connsm->cssm->config_req_action;
    const struct ble_ll_cs_config *conf;

    assert(config_id < ARRAY_SIZE(connsm->cssm->config));

    *dptr = config_id | action << 6;

    if (action == 0x00) {
        /* Removing the config, all remaining fields are RFU. */
        memset(dptr + 1, 0, 26);

        return;
    }

    conf = &connsm->cssm->tmp_config;
    memcpy(dptr + 1, conf->chan_map, 10);
    dptr[11] = conf->chan_map_repetition;
    dptr[12] = conf->main_mode;
    dptr[13] = conf->sub_mode;
    dptr[14] = conf->main_mode_min_steps;
    dptr[15] = conf->main_mode_max_steps;
    dptr[16] = conf->main_mode_repetition;
    dptr[17] = conf->mode_0_steps;
    dptr[18] = conf->cs_sync_phy;
    dptr[19] = conf->rtt_type |
               conf->role << 4;
    dptr[20] = conf->chan_sel |
               conf->ch3cshape << 4;
    dptr[21] = conf->ch3cjump;
    dptr[22] = conf->t_ip1_index;
    dptr[23] = conf->t_ip2_index;
    dptr[24] = conf->t_fcs_index;
    dptr[25] = conf->t_pm_index;
    /* RFU octet */
    dptr[26] = 0x00;
}

static int
ble_ll_cs_verify_config(struct ble_ll_cs_config *conf)
{
    if (ble_ll_cs_verify_chan_map(conf->chan_map)) {
        return 1;
    }

    if (conf->chan_map_repetition < 1) {
        return 1;
    }

    /* Valid combinations of Main_Mode and Sub_Mode selections */
    if (conf->main_mode == 0x01) {
        if (conf->sub_mode != 0xFF) {
            return 1;
        }
    } else if (conf->main_mode == 0x02) {
        if (conf->sub_mode != 0x01 &&
            conf->sub_mode != 0x03 &&
            conf->sub_mode != 0xFF) {
            return 1;
        }
    } else if (conf->main_mode == 0x03) {
        if (conf->sub_mode != 0x02 &&
            conf->sub_mode != 0xFF) {
            return 1;
        }
    } else {
        return 1;
    }

    if (conf->sub_mode == 0xFF) {
        /* RFU if Sub_Mode is None */
        conf->main_mode_min_steps = 0x00;
        conf->main_mode_max_steps = 0x00;
    }

    if (conf->main_mode_repetition > 0x03) {
        return 1;
    }

    if (conf->mode_0_steps < 1 || conf->mode_0_steps > 3) {
        return 1;
    }

    if (conf->cs_sync_phy != 0b0001 &&
        conf->cs_sync_phy != 0b0010 &&
        conf->cs_sync_phy != 0b0100 &&
        conf->cs_sync_phy != 0b1000) {
        return 1;
    }

    if (conf->rtt_type > 0x06) {
        return 1;
    }

    if (conf->chan_sel > 0x01) {
        return 1;
    }

    if (conf->chan_sel == 0x01) {
        if (conf->ch3cshape > 0x01) {
            return 1;
        }

        if (!IN_RANGE(conf->ch3cjump, 2, 8)) {
            return 1;
        }
    }

    if (conf->t_ip1_index > 7) {
        return 1;
    }

    if (conf->t_ip2_index > 7) {
        return 1;
    }

    if (conf->t_fcs_index > 9) {
        return 1;
    }

    if (conf->t_pm_index > 2) {
        return 1;
    }

    return 0;
}

static void
ble_ll_cs_ev_config_complete(struct ble_ll_conn_sm *connsm, uint8_t config_id,
                             uint8_t action, uint8_t status)
{
    struct ble_hci_ev_le_subev_cs_config_complete *ev;
    const struct ble_ll_cs_config *conf;
    struct ble_hci_ev *hci_ev;

    if (ble_ll_hci_is_le_event_enabled(
            BLE_HCI_LE_SUBEV_CS_CONFIG_COMPLETE)) {
        hci_ev = ble_transport_alloc_evt(0);
        if (hci_ev) {
            hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
            hci_ev->length = sizeof(*ev);
            ev = (void *) hci_ev->data;

            memset(ev, 0, sizeof(*ev));
            ev->subev_code = BLE_HCI_LE_SUBEV_CS_CONFIG_COMPLETE;
            ev->status = status;
            ev->conn_handle = htole16(connsm->conn_handle);
            ev->config_id = config_id;
            ev->action = action;

            if (action != 0x00 && status == BLE_ERR_SUCCESS) {
                conf = &connsm->cssm->config[config_id];
                ev->main_mode_type = conf->main_mode;
                ev->sub_mode_type = conf->sub_mode;
                ev->min_main_mode_steps = conf->main_mode_min_steps;
                ev->max_main_mode_steps = conf->main_mode_max_steps;
                ev->main_mode_repetition = conf->main_mode_repetition;
                ev->mode_0_steps = conf->mode_0_steps;
                ev->role = conf->role;
                ev->rtt_type = conf->rtt_type;
                ev->cs_sync_phy = conf->cs_sync_phy;
                memcpy(ev->channel_map, conf->chan_map, 10);
                ev->channel_map_repetition = conf->chan_map_repetition;
                ev->channel_selection_type = conf->chan_sel;
                ev->ch3c_shape = conf->ch3cshape;
                ev->ch3c_jump = conf->ch3cjump;
                ev->reserved = 0x00;
                ev->t_ip1_time = conf->t_ip1;
                ev->t_ip2_time = conf->t_ip2;
                ev->t_fcs_time = conf->t_fcs;
                ev->t_pm_time = conf->t_pm;
            }

            ble_ll_hci_event_send(hci_ev);
        }
    }
}

int
ble_ll_cs_rx_config_req(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                        uint8_t *rspbuf)
{
    struct ble_ll_cs_config *conf;
    uint8_t config_id = *dptr & 0b00111111;
    uint8_t action = (*dptr & 0b11000000) >> 6;
    struct ble_ll_cs_sm *cssm = connsm->cssm;
    struct ble_ll_cs_pref_proc_params *pps;

    if (IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CS_CONF)) {
        if (CONN_IS_CENTRAL(connsm)) {
            /* Reject CS config initiated by peripheral */
            ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_CONFIG_REQ,
                                         BLE_ERR_LMP_COLLISION, rspbuf);
            return BLE_LL_CTRL_REJECT_IND_EXT;
        } else {
            /* Take no further action in the Peripheral-initiated procedure
             * and proceed to handle the Central-initiated procedure.
             */
            ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_CONF);
        }
    }

    if (config_id >= ARRAY_SIZE(cssm->config)) {
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_CONFIG_REQ,
                                     BLE_ERR_INV_LMP_LL_PARM, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    conf = &cssm->config[config_id];
    if (conf->config_in_use) {
        /* CS procedure in progress exit with error */
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_CONFIG_REQ,
                                     BLE_ERR_CMD_DISALLOWED, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    /* Respond with LL_CS_CONFIG_RSP PDU */
    *rspbuf = config_id;

    if (action == 0x00) {
        /* CS configuration removed. */
        memset(conf, 0, sizeof(*conf));
        ble_ll_cs_ev_config_complete(connsm, config_id, 0, BLE_ERR_SUCCESS);
        return BLE_LL_CTRL_CS_CONFIG_RSP;
    }

    conf = &cssm->tmp_config;
    memset(conf, 0, sizeof(*conf));
    memcpy(conf->chan_map, dptr + 1, 10);
    conf->chan_map_repetition = dptr[11];
    conf->main_mode = dptr[12];
    conf->sub_mode = dptr[13];
    conf->main_mode_min_steps = dptr[14];
    conf->main_mode_max_steps = dptr[15];
    conf->main_mode_repetition = dptr[16];
    conf->mode_0_steps = dptr[17];
    conf->cs_sync_phy = dptr[18];
    conf->rtt_type = dptr[19] & 0b00001111;
    conf->role = (~dptr[19] >> 4) & 0b00000001;
    conf->chan_sel = (dptr[20] & 0b00001111);
    conf->ch3cshape = (dptr[20] & 0b11110000) >> 4;
    conf->ch3cjump = dptr[21];
    conf->t_ip1_index = dptr[22];
    conf->t_ip2_index = dptr[23];
    conf->t_fcs_index = dptr[24];
    conf->t_pm_index = dptr[25];

    if (ble_ll_cs_verify_config(conf)) {
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_CONFIG_REQ,
                                     BLE_ERR_UNSUPP_LMP_LL_PARM, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    conf->t_ip1 = t_ip1[conf->t_ip1_index];
    conf->t_ip2 = t_ip2[conf->t_ip2_index];
    conf->t_fcs = t_fcs[conf->t_fcs_index];
    conf->t_pm = t_pm[conf->t_pm_index];
    conf->config_enabled = 1;

    pps = &conf->pref_proc_params;
    pps->min_subevent_len = BLE_LL_CS_SUBEVENT_LEN_MIN;
    pps->max_subevent_len = BLE_LL_CS_SUBEVENT_LEN_MAX;
    pps->max_procedure_len = BLE_LL_CS_PROCEDURE_LEN_MAX;
    pps->min_procedure_interval = BLE_LL_CS_PROCEDURE_INTERVAL_MIN;
    pps->max_procedure_interval = BLE_LL_CS_PROCEDURE_INTERVAL_MAX;
    pps->max_procedure_count = BLE_LL_CS_PROCEDURE_COUNT_NO_LIMIT;
    pps->aci = 0x01;
    pps->phy = 0x01;
    pps->tx_power_delta = 0x80;
    pps->preferred_peer_antenna = 0b0001;
    pps->snr_control_initiator = 0xFF;
    pps->snr_control_reflector = 0xFF;
    pps->params_ready = 1;

    memcpy(&cssm->config[config_id], conf, sizeof(*conf));
    memset(conf, 0, sizeof(*conf));

    ble_ll_cs_ev_config_complete(connsm, config_id, 1, BLE_ERR_SUCCESS);

    return BLE_LL_CTRL_CS_CONFIG_RSP;
}

void
ble_ll_cs_rx_config_rsp(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    struct ble_ll_cs_config *conf;
    struct ble_ll_cs_pref_proc_params *pps;
    uint8_t config_id = *dptr & 0b00111111;
    struct ble_ll_cs_sm *cssm = connsm->cssm;

    if (config_id != cssm->config_req_id ||
        !IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CS_CONF)) {
        return;
    }

    /* Configure CS config locally */
    conf = &cssm->config[config_id];
    memcpy(conf, &cssm->tmp_config, sizeof(cssm->tmp_config));
    memset(&cssm->tmp_config, 0, sizeof(cssm->tmp_config));
    pps = &conf->pref_proc_params;
    pps->min_subevent_len = BLE_LL_CS_SUBEVENT_LEN_MIN;
    pps->max_subevent_len = BLE_LL_CS_SUBEVENT_LEN_MAX;
    pps->max_procedure_len = BLE_LL_CS_PROCEDURE_LEN_MAX;
    pps->min_procedure_interval = BLE_LL_CS_PROCEDURE_INTERVAL_MIN;
    pps->max_procedure_interval = BLE_LL_CS_PROCEDURE_INTERVAL_MAX;
    pps->max_procedure_count = BLE_LL_CS_PROCEDURE_COUNT_NO_LIMIT;
    pps->aci = 0x01;
    pps->phy = 0x01;
    pps->tx_power_delta = 0x80;
    pps->preferred_peer_antenna = 0b0001;
    pps->snr_control_initiator = 0xFF;
    pps->snr_control_reflector = 0xFF;
    pps->params_ready = 1;

    /* Stop the control procedure and send an event to the host */
    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_CONF);
    ble_ll_cs_ev_config_complete(connsm, config_id, cssm->config_req_action, BLE_ERR_SUCCESS);
}

void
ble_ll_cs_rx_config_req_rejected(struct ble_ll_conn_sm *connsm, uint8_t ble_error)
{
    struct ble_ll_cs_sm *cssm = connsm->cssm;

    memset(&cssm->tmp_config, 0, sizeof(cssm->tmp_config));

    /* Stop the control procedure and send an event to the host */
    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_CONF);
    ble_ll_cs_ev_config_complete(connsm, cssm->config_req_id,
                                 cssm->config_req_action, ble_error);
}

static int
ble_ll_cs_select_capability(uint8_t capability_values_count,
                            uint8_t *out_index, uint16_t local_capability,
                            uint16_t remote_capability)
{
    uint16_t common_capability = local_capability & remote_capability;
    uint8_t i;

    for (i = 0; i < capability_values_count; i++) {
        if ((common_capability >> i) & 1) {
            *out_index = i;
            return 0;
        }
    }

    return 1;
}

int
ble_ll_cs_hci_create_config(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    const struct ble_hci_le_cs_create_config_cp *cmd = (const void *)cmdbuf;
    struct ble_ll_cs_pref_proc_params *pps;
    struct ble_ll_conn_sm *connsm;
    struct ble_ll_cs_sm *cssm;
    struct ble_ll_cs_config *conf;

    if (cmdlen != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* If no connection handle exit with error */
    connsm = ble_ll_conn_find_by_handle(le16toh(cmd->conn_handle));
    if (!connsm) {
        return BLE_ERR_UNK_CONN_ID;
    }

    cssm = connsm->cssm;
    if (cmd->config_id >= ARRAY_SIZE(cssm->config)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    conf = &cssm->config[cmd->config_id];

    /* If already pending or CS procedure in progress exit with error */
    if (IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CS_CONF) ||
        conf->config_in_use) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Save the CS configuration in temporary variable as the config
     * might be rejected by the remote.
     */
    conf = &cssm->tmp_config;
    memset(conf, 0, sizeof(*conf));
    conf->config_enabled = 1;
    conf->main_mode = cmd->main_mode_type;
    conf->sub_mode = cmd->sub_mode_type;
    conf->main_mode_min_steps = cmd->min_main_mode_steps;
    conf->main_mode_max_steps = cmd->max_main_mode_steps;
    conf->main_mode_repetition = cmd->main_mode_repetition;
    conf->mode_0_steps = cmd->mode_0_steps;
    conf->role = cmd->role;
    conf->rtt_type = cmd->rtt_type;
    conf->cs_sync_phy = cmd->cs_sync_phy;
    memcpy(conf->chan_map, cmd->channel_map, 10);
    conf->chan_map_repetition = cmd->channel_map_repetition;
    conf->chan_sel = cmd->channel_selection_type;
    conf->ch3cshape = cmd->ch3c_shape;
    conf->ch3cjump = cmd->ch3c_jump;

    pps = &conf->pref_proc_params;
    pps->min_subevent_len = BLE_LL_CS_SUBEVENT_LEN_MIN;
    pps->max_subevent_len = BLE_LL_CS_SUBEVENT_LEN_MAX;

    if (ble_ll_cs_select_capability(ARRAY_SIZE(t_ip1), &conf->t_ip1_index,
                                    cssm->remote_cap.t_ip1_capability | (1 << T_IP1_CAP_ID_145US),
                                    g_ble_ll_cs_local_cap.t_ip1_capability | (1 << T_IP1_CAP_ID_145US))) {
        memset(conf, 0, sizeof(*conf));
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (ble_ll_cs_select_capability(ARRAY_SIZE(t_ip2), &conf->t_ip2_index,
                                    cssm->remote_cap.t_ip2_capability | (1 << T_IP2_CAP_ID_145US),
                                    g_ble_ll_cs_local_cap.t_ip2_capability | (1 << T_IP2_CAP_ID_145US))) {
        memset(conf, 0, sizeof(*conf));
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (ble_ll_cs_select_capability(ARRAY_SIZE(t_fcs), &conf->t_fcs_index,
                                    cssm->remote_cap.t_fcs_capability | (1 << T_FCS_CAP_ID_150US),
                                    g_ble_ll_cs_local_cap.t_fcs_capability | (1 << T_FCS_CAP_ID_150US))) {
        memset(conf, 0, sizeof(*conf));
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (ble_ll_cs_select_capability(ARRAY_SIZE(t_pm), &conf->t_pm_index,
                                    cssm->remote_cap.t_pm_capability | (1 << T_PM_CAP_ID_40US),
                                    g_ble_ll_cs_local_cap.t_pm_capability | (1 << T_PM_CAP_ID_40US))) {
        memset(conf, 0, sizeof(*conf));
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    conf->t_ip1 = t_ip1[conf->t_ip1_index];
    conf->t_ip2 = t_ip2[conf->t_ip2_index];
    conf->t_fcs = t_fcs[conf->t_fcs_index];
    conf->t_pm = t_pm[conf->t_pm_index];

    if (ble_ll_cs_verify_config(conf)) {
        memset(conf, 0, sizeof(*conf));
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (cmd->create_context == 0x01) {
        /* Configure the CS config in the remote controller */
        cssm->config_req_id = cmd->config_id;
        cssm->config_req_action = 0x01;
        ble_ll_ctrl_proc_start(connsm, BLE_LL_CTRL_PROC_CS_CONF);
    } else {
        ble_ll_cs_ev_config_complete(connsm, cmd->config_id, 0x01, BLE_ERR_SUCCESS);
    }

    return BLE_ERR_SUCCESS;
}

int
ble_ll_cs_hci_remove_config(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    const struct ble_hci_le_cs_remove_config_cp *cmd = (const void *)cmdbuf;
    struct ble_ll_conn_sm *connsm;
    struct ble_ll_cs_sm *cssm;
    struct ble_ll_cs_config *conf;

    if (cmdlen != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* If no connection handle exit with error */
    connsm = ble_ll_conn_find_by_handle(le16toh(cmd->conn_handle));
    if (!connsm) {
        return BLE_ERR_UNK_CONN_ID;
    }

    cssm = connsm->cssm;
    if (cmd->config_id >= ARRAY_SIZE(cssm->config)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    conf = &cssm->config[cmd->config_id];

    /* If already pending or CS procedure in progress exit with error */
    if (IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CS_CONF) ||
        conf->config_in_use) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* Remove the CS config locally */
    memset(conf, 0, sizeof(*conf));

    /* Configure the CS config in the remote controller */
    cssm->config_req_id = cmd->config_id;
    cssm->config_req_action = 0x00;
    ble_ll_ctrl_proc_start(connsm, BLE_LL_CTRL_PROC_CS_CONF);

    return BLE_ERR_SUCCESS;
}

static int
ble_ll_cs_proc_set_chan_class(const uint8_t *channel_classification)
{
    /* TODO:
     * 1. The interval between two successive commands sent shall be at least 1 second.
     * Otherwise, the Controller shall return the error code Command Disallowed (0x0C).
     *
     * 2. Combine the Host chan_class with local chan_class capabilities?
     */

    if (ble_ll_cs_verify_chan_map(channel_classification)) {
        return -1;
    }

    memcpy(g_ble_ll_cs_chan_class, channel_classification,
           sizeof(g_ble_ll_cs_chan_class));

    return 0;
}

int
ble_ll_cs_hci_set_chan_class(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    int rc;
    const struct ble_hci_le_cs_set_chan_class_cp *cmd = (const void *)cmdbuf;

    rc = ble_ll_cs_proc_set_chan_class(cmd->channel_classification);
    if (rc) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    return BLE_ERR_SUCCESS;
}

int
ble_ll_cs_hci_set_proc_params(const uint8_t *cmdbuf, uint8_t cmdlen,
                              uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_cs_set_proc_params_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_cs_set_proc_params_rp *rsp = (void *)rspbuf;
    struct ble_ll_conn_sm *connsm;
    struct ble_ll_cs_config *conf;
    struct ble_ll_cs_pref_proc_params *params;

    if (cmdlen != sizeof(*cmd)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* If no connection handle exit with error */
    connsm = ble_ll_conn_find_by_handle(le16toh(cmd->conn_handle));
    if (!connsm) {
        return BLE_ERR_UNK_CONN_ID;
    }

    if (cmd->config_id >= ARRAY_SIZE(connsm->cssm->config)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    conf = &connsm->cssm->config[cmd->config_id];

    /* If CS configuration with Config_ID does not exists */
    if (!conf->config_enabled) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* If CS measurement is enabled exit with error */
    if (connsm->cssm->measurement_enabled) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    if (ble_ll_cs_proc_params_channels_setup(conf)) {
        return BLE_ERR_INSUFFICIENT_CHAN;
    }

    params = &conf->pref_proc_params;
    params->max_procedure_len = htole16(cmd->max_procedure_len);
    params->min_procedure_interval = htole16(cmd->min_procedure_interval);
    params->max_procedure_interval = htole16(cmd->max_procedure_interval);
    params->max_procedure_count = htole16(cmd->max_procedure_count);
    params->min_subevent_len = get_le24(cmd->min_subevent_len);
    params->max_subevent_len = get_le24(cmd->max_subevent_len);
    params->aci = cmd->tone_antenna_config_selection;
    params->phy = cmd->phy;
    params->tx_power_delta = (cmd->tx_power_delta == 0x80) ? 0 : cmd->tx_power_delta;
    params->preferred_peer_antenna = cmd->preferred_peer_antenna;
    params->snr_control_initiator = cmd->snr_control_initiator & 0x0F;
    params->snr_control_reflector = cmd->snr_control_reflector & 0x0F;
    params->params_ready = 1;

    rsp->conn_handle = cmd->conn_handle;
    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

int
ble_ll_cs_hci_proc_enable(const uint8_t *cmdbuf, uint8_t cmdlen)
{
    const struct ble_hci_le_cs_proc_enable_cp *cmd = (const void *)cmdbuf;
    struct ble_ll_conn_sm *connsm;
    struct ble_ll_cs_config *conf;

    if (cmdlen != sizeof(*cmd) || cmd->config_id >= ARRAY_SIZE(connsm->cssm->config)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* If no connection handle exit with error */
    connsm = ble_ll_conn_find_by_handle(le16toh(cmd->conn_handle));
    if (!connsm) {
        return BLE_ERR_UNK_CONN_ID;
    }

    conf = &connsm->cssm->config[cmd->config_id];
    if (!conf->config_enabled) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (cmd->enable) {
        if (!conf->pref_proc_params.params_ready ||
            connsm->cssm->measurement_enabled) {
            return BLE_ERR_CMD_DISALLOWED;
        }

        memset(&conf->proc_params, 0, sizeof(conf->proc_params));
        if (ble_ll_cs_proc_params_channels_setup(conf)) {
            return BLE_ERR_INSUFFICIENT_CHAN;
        }

        /* Start scheduling CS procedures */
        ble_ll_ctrl_proc_start(connsm, BLE_LL_CTRL_PROC_CS_START);
    } else {
        if (!connsm->cssm->measurement_enabled) {
            return BLE_ERR_CMD_DISALLOWED;
        }

        connsm->cssm->terminate_procedure_id = connsm->cssm->pending_procedure_id;
        connsm->cssm->terminate_measurement = 1;
        connsm->cssm->terminate_config_id = cmd->config_id;
        connsm->cssm->terminate_error_code = BLE_ERR_SUCCESS;

        /* Terminate the CS measurement early */
        ble_ll_ctrl_proc_start(connsm, BLE_LL_CTRL_PROC_CS_TERMINATE);
    }

    return BLE_ERR_SUCCESS;
}

static void
ble_ll_cs_ev_cs_proc_enable_complete(struct ble_ll_conn_sm *connsm,
                                     uint8_t config_id, uint8_t status)
{
    struct ble_hci_ev_le_subev_cs_proc_enable_complete *ev;
    const struct ble_ll_cs_proc_params *params;
    struct ble_hci_ev *hci_ev;

    if (ble_ll_hci_is_le_event_enabled(
            BLE_HCI_LE_SUBEV_CS_PROC_ENABLE_COMPLETE)) {
        hci_ev = ble_transport_alloc_evt(0);
        if (hci_ev) {
            hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
            hci_ev->length = sizeof(*ev);
            ev = (void *) hci_ev->data;

            memset(ev, 0, sizeof(*ev));
            ev->subev_code = BLE_HCI_LE_SUBEV_CS_PROC_ENABLE_COMPLETE;
            ev->status = status;
            ev->conn_handle = htole16(connsm->conn_handle);
            ev->config_id = config_id;
            ev->state = connsm->cssm->measurement_enabled;

            if (status == BLE_ERR_SUCCESS) {
                params = &connsm->cssm->config[config_id].proc_params;
                ev->tone_antenna_config_selection = params->aci;
                ev->selected_tx_power = g_ble_ll_tx_power;
                put_le24(ev->subevent_len, params->subevent_len);
                ev->subevents_per_event = params->subevents_per_event;
                ev->subevent_interval = htole16(params->subevent_interval);
                ev->event_interval = htole16(params->event_interval);
                ev->procedure_interval = htole16(params->procedure_interval);
                ev->procedure_count = htole16(connsm->cssm->pending_procedure_id);
            }

            ble_ll_hci_event_send(hci_ev);
        }
    }
}

void
ble_ll_cs_start_req_parameters_setup(struct ble_ll_cs_proc_params *ps,
                                     struct ble_ll_cs_pref_proc_params *pps,
                                     uint32_t conn_itvl_us, uint32_t ce_duration_us,
                                     uint16_t subrate_base_event, uint16_t subrate_factor,
                                     uint16_t event_cntr)
{
    uint32_t max_procedure_len_us;
    uint32_t time_until_next_ce_us;
    uint32_t min_subevent_len;
    uint32_t subevents_available_us;
    uint32_t subevents_remainder_us;
    uint32_t subevent_interval_us;

    /* Use Host preferences */
    ps->max_procedure_count = pps->max_procedure_count;
    ps->max_procedure_len = pps->max_procedure_len;
    ps->procedure_interval = pps->max_procedure_interval;
    ps->aci = pps->aci;
    ps->preferred_peer_antenna = pps->preferred_peer_antenna;
    ps->phy = pps->phy;
    ps->tx_power_delta = pps->tx_power_delta;
    ps->tx_snr_i = pps->snr_control_initiator;
    ps->tx_snr_r = pps->snr_control_reflector;

    /* The extent of a CS subevent may exceed that of the underlying
     * LE connection interval. To avoid a collision with a CE, let's set
     * the anchor event_cntr to be relatively offset from subrate_base_event
     * by a multiple of subrate_factor.
     */
    if (subrate_factor > 1) {
        if (MIN_CONN_EVENT_COUNT_BEFORE_START > subrate_factor) {
            event_cntr = subrate_base_event + div_ceil(MIN_CONN_EVENT_COUNT_BEFORE_START,
                                                       subrate_factor) * subrate_factor;
        } else {
            event_cntr = subrate_base_event + subrate_factor;
        }
    } else {
        event_cntr = event_cntr + MIN_CONN_EVENT_COUNT_BEFORE_START;
    }

    ps->anchor_conn_event_cntr = event_cntr;

    /* The earliest time the radio scheduler will be ready after CE */
    ps->offset_min = MAX(BLE_LL_CS_EVENT_OFFSET_MIN_US, ce_duration_us +
                         BLE_LL_CS_SUBEVENT_SAFE_SPACE_FROM_CE);

    max_procedure_len_us = ps->max_procedure_len * BLE_LL_CS_PROCEDURE_LEN_UNIT_US;
    /* The total time we can use for subevents before next CE */
    time_until_next_ce_us = conn_itvl_us * subrate_factor;
    time_until_next_ce_us = MIN(max_procedure_len_us, time_until_next_ce_us);
    subevents_available_us = time_until_next_ce_us - ps->offset_min;

    /* Use Host preferences as boundary values for subevent_len */
    min_subevent_len = MAX(BLE_LL_CS_SUBEVENT_LEN_MIN, pps->min_subevent_len);
    ps->subevent_len = MIN(subevents_available_us - BLE_LL_CS_SUBEVENT_T_MES_US,
                           pps->max_subevent_len);
    ps->subevent_len = CLAMP(ps->subevent_len, min_subevent_len, BLE_LL_CS_SUBEVENT_LEN_MAX);

    BLE_LL_CS_ASSERT(time_until_next_ce_us > min_subevent_len + BLE_LL_CS_SUBEVENT_T_MES_US);

    ps->offset_max = MIN(BLE_LL_CS_EVENT_OFFSET_MAX_US, time_until_next_ce_us -
                         min_subevent_len - BLE_LL_CS_SUBEVENT_T_MES_US);

    subevent_interval_us = ps->subevent_len + BLE_LL_CS_SUBEVENT_MIN_SPACING_US;

    ps->subevents_per_event = subevents_available_us / subevent_interval_us;
    if (BLE_LL_CS_SUBEVENTS_PER_EVENT_MAX <= ps->subevents_per_event) {
        ps->subevents_per_event = BLE_LL_CS_SUBEVENTS_PER_EVENT_MAX;
    } else {
        subevents_remainder_us = subevents_available_us - ps->subevents_per_event * subevent_interval_us;
        if (min_subevent_len <= subevents_remainder_us) {
            ps->subevents_per_event++;
        }
    }

    if (ps->subevents_per_event <= 1) {
        ps->subevents_per_event = 1;
        ps->subevent_interval = 0;
    } else {
        ps->subevent_interval = div_ceil(subevent_interval_us,
                                         BLE_LL_CS_SUBEVENTS_INTERVAL_UNIT_US);
    }

    ps->event_interval = subrate_factor;
}

void
ble_ll_cs_start_req_make(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    uint8_t config_id = connsm->cssm->config_req_id;
    struct ble_ll_cs_pref_proc_params *pps;
    struct ble_ll_cs_proc_params *ps;
    uint32_t ce_duration_us;
    uint32_t conn_itvl_us;
    uint16_t subrate_base_event;
    uint16_t subrate_factor;

    assert(config_id < ARRAY_SIZE(connsm->cssm->config));
    pps = &connsm->cssm->config[config_id].pref_proc_params;
    ps = &connsm->cssm->config[config_id].proc_params;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_ENHANCED_CONN_UPDATE)
    subrate_factor = connsm->subrate_factor;
    subrate_base_event = connsm->subrate_base_event;
#else
    subrate_factor = 1;
    subrate_base_event = connsm->event_cntr;
#endif

    conn_itvl_us = ble_ll_tmr_t2u(connsm->conn_itvl_ticks);
    ce_duration_us = ble_ll_tmr_t2u(connsm->conn_sch.end_time - connsm->conn_sch.start_time);

    ble_ll_cs_start_req_parameters_setup(ps, pps, conn_itvl_us, ce_duration_us, subrate_base_event,
                                         subrate_factor, connsm->event_cntr);

    *dptr = config_id;
    put_le16(dptr + 1, ps->anchor_conn_event_cntr);
    put_le24(dptr + 3, ps->offset_min);
    put_le24(dptr + 6, ps->offset_max);
    put_le16(dptr + 9, ps->max_procedure_len);
    put_le16(dptr + 11, ps->event_interval);
    dptr[13] = ps->subevents_per_event;
    put_le16(dptr + 14, ps->subevent_interval);
    put_le24(dptr + 16, ps->subevent_len);
    put_le16(dptr + 19, ps->procedure_interval);
    put_le16(dptr + 21, ps->max_procedure_count);
    dptr[23] = ps->aci;
    dptr[24] = ps->preferred_peer_antenna;
    dptr[25] = ps->phy;
    dptr[26] = ps->tx_power_delta;
    dptr[27] = ps->tx_snr_i |
               ps->tx_snr_r << 4;
}

static int
ble_ll_cs_start_ind_make(struct ble_ll_conn_sm *connsm, uint8_t *rspbuf)
{
    uint8_t config_id = connsm->cssm->config_req_id;
    struct ble_ll_cs_proc_params *ps;

    assert(config_id < ARRAY_SIZE(connsm->cssm->config));
    ps = &connsm->cssm->config[config_id].proc_params;

    *rspbuf = config_id;
    put_le16(rspbuf + 1, ps->anchor_conn_event_cntr);
    put_le24(rspbuf + 3, ps->event_offset);
    put_le16(rspbuf + 6, ps->event_interval);
    rspbuf[8] = ps->subevents_per_event;
    put_le16(rspbuf + 9, ps->subevent_interval);
    put_le24(rspbuf + 11, ps->subevent_len);
    rspbuf[14] = ps->aci;
    rspbuf[15] = ps->phy;
    rspbuf[16] = ps->tx_power_delta;
    rspbuf[17] = 0x00;

    return BLE_LL_CTRL_CS_IND;
}

static int
ble_ll_cs_start_rsp_make(struct ble_ll_conn_sm *connsm, uint8_t *rspbuf)
{
    uint8_t config_id = connsm->cssm->config_req_id;
    struct ble_ll_cs_proc_params *ps;

    assert(config_id < ARRAY_SIZE(connsm->cssm->config));
    ps = &connsm->cssm->config[config_id].proc_params;

    *rspbuf = config_id;
    put_le16(rspbuf + 1, ps->anchor_conn_event_cntr);
    put_le24(rspbuf + 3, ps->offset_min);
    put_le24(rspbuf + 6, ps->offset_max);
    put_le16(rspbuf + 9, ps->event_interval);
    rspbuf[11] = ps->subevents_per_event;
    put_le16(rspbuf + 12, ps->subevent_interval);
    put_le24(rspbuf + 14, ps->subevent_len);
    rspbuf[17] = ps->aci;
    rspbuf[18] = ps->phy;
    rspbuf[19] = ps->tx_power_delta;
    /* 1 RFU octet */
    rspbuf[20] = 0x00;

    return BLE_LL_CTRL_CS_RSP;
}

static void
ble_ll_cs_offset_clamp(uint32_t loc_offset_min, uint32_t loc_offset_max,
                       uint32_t rem_offset_min, uint32_t rem_offset_max,
                       uint32_t *res_offset_min, uint32_t *res_offset_max)
{
    if (loc_offset_max < rem_offset_min || rem_offset_max < loc_offset_min) {
        *res_offset_min = loc_offset_min;
        *res_offset_max = loc_offset_max;
    } else {
        *res_offset_min = MAX(loc_offset_min, rem_offset_min);
        *res_offset_max = MIN(loc_offset_max, rem_offset_max);
    }
}

uint16_t
round_up_to_subrate_factor(uint16_t event, uint16_t ref_event, uint16_t subrate_factor)
{
    uint16_t result_event = event;
    uint16_t align_subrate;

    if (ref_event < event) {
        align_subrate = (event - ref_event) % subrate_factor;
        if (align_subrate != 0) {
            result_event += subrate_factor - align_subrate;
        }
    } else {
        result_event = ref_event;
    }

    return result_event;
}

int
ble_ll_cs_start_req_parameters_apply(struct ble_ll_cs_proc_params *ps, uint32_t conn_itvl_us,
                                     uint32_t ce_duration_us, uint16_t subrate_base_event,
                                     uint16_t subrate_factor, uint16_t event_cntr)
{
    uint32_t offset_min;
    uint32_t offset_max;
    uint32_t max_procedure_len_us;
    uint32_t time_until_next_ce_us;
    uint32_t subevent_interval_us;
    uint32_t event_interval_us;
    uint32_t align_subrate;

    if (subrate_factor > 1) {
        /* Use the multiple of subrate_factor to make sure we will not overlap a CE */
        if (MIN_CONN_EVENT_COUNT_BEFORE_START > subrate_factor) {
            event_cntr = subrate_base_event + div_ceil(MIN_CONN_EVENT_COUNT_BEFORE_START,
                                                       subrate_factor) * subrate_factor;
        } else {
            event_cntr = subrate_base_event + subrate_factor;
        }
    } else {
        event_cntr = event_cntr + MIN_CONN_EVENT_COUNT_BEFORE_START;
    }

    ps->anchor_conn_event_cntr = round_up_to_subrate_factor(ps->anchor_conn_event_cntr,
                                                            event_cntr,
                                                            subrate_factor);

    offset_min = MAX(BLE_LL_CS_EVENT_OFFSET_MIN_US, ce_duration_us +
                     BLE_LL_CS_SUBEVENT_SAFE_SPACE_FROM_CE);

    max_procedure_len_us = ps->max_procedure_len * BLE_LL_CS_PROCEDURE_LEN_UNIT_US;
    /* The total time we can use for subevents before next CE */
    time_until_next_ce_us = conn_itvl_us * subrate_factor;
    time_until_next_ce_us = MIN(max_procedure_len_us, time_until_next_ce_us);

    BLE_LL_CS_ASSERT(time_until_next_ce_us > BLE_LL_CS_SUBEVENT_LEN_MIN + BLE_LL_CS_SUBEVENT_T_MES_US);

    offset_max = MIN(BLE_LL_CS_EVENT_OFFSET_MAX_US, time_until_next_ce_us -
                     BLE_LL_CS_SUBEVENT_LEN_MIN - BLE_LL_CS_SUBEVENT_T_MES_US);
    ble_ll_cs_offset_clamp(offset_min, offset_max,
                           ps->offset_min, ps->offset_max,
                           &ps->offset_min, &ps->offset_max);

    ps->subevent_len = MIN(MIN(time_until_next_ce_us - offset_min - BLE_LL_CS_SUBEVENT_T_MES_US,
                               BLE_LL_CS_SUBEVENT_LEN_MAX), ps->subevent_len);
    ps->event_offset = ps->offset_min;

    ps->subevents_per_event = MIN(BLE_LL_CS_SUBEVENTS_PER_EVENT_MAX, ps->subevents_per_event);
    if (ps->subevents_per_event == 1) {
        ps->subevent_interval = 0;
    } else if (ps->subevent_interval == 0) {
        ps->subevents_per_event = 1;
    } else {
        subevent_interval_us = ps->subevent_interval * BLE_LL_CS_SUBEVENTS_INTERVAL_UNIT_US;
        if (subevent_interval_us < ps->subevent_len + BLE_LL_CS_SUBEVENT_MIN_SPACING_US) {
            subevent_interval_us = ps->subevent_len + BLE_LL_CS_SUBEVENT_MIN_SPACING_US;
            ps->subevent_interval = div_ceil(subevent_interval_us,
                                             BLE_LL_CS_SUBEVENTS_INTERVAL_UNIT_US);
        }

        if (ps->max_procedure_len < ps->subevent_interval) {
            ps->subevent_interval = 0;
            ps->subevents_per_event = 1;
        }

        event_interval_us = ps->event_interval * conn_itvl_us;
        if (event_interval_us < subevent_interval_us) {
            ps->event_interval = subrate_factor;
        }
    }

    if (subrate_factor < ps->event_interval) {
        align_subrate = (ps->event_interval - subrate_factor) % subrate_factor;
        if (align_subrate != 0) {
            ps->event_interval += subrate_factor - align_subrate;
        }
    }

    if (ps->aci > 7) {
        /* Resuggest ACI with ID 0 as the controller does not support antenna switching yet */
        ps->aci = 0;
    }

    if (ps->phy != 0b0001 && ps->phy != 0b0010) {
        /* Resuggest using LE 1M PHY */
        ps->phy = 1;
    }

    if (!(IN_RANGE(ps->tx_power_delta, 0x00, 0x14) ||
          IN_RANGE(ps->tx_power_delta, 0x7E, 0xFF))) {
        /* Invalid range. Resuggest to not change the power level */
        ps->tx_power_delta = 0x00;
    }

    if ((ps->tx_snr_i > 4 && ps->tx_snr_i != 0xF) ||
        (ps->tx_snr_r > 4 && ps->tx_snr_r != 0xF) ||
        ps->preferred_peer_antenna == 0) {
        /* We cannot resuggest these, so reject them */
        return 1;
    }

    return 0;
}

int
ble_ll_cs_start_rsp_parameters_apply(struct ble_ll_cs_proc_params *ps,
                                     struct ble_ll_cs_proc_params *rx_ps, uint32_t conn_itvl_us,
                                     uint32_t ce_duration_us, uint16_t subrate_factor,
                                     uint16_t event_cntr)
{
    uint32_t subevent_interval_us;
    uint32_t event_interval_us;

    ps->anchor_conn_event_cntr = round_up_to_subrate_factor(rx_ps->anchor_conn_event_cntr,
                                                            ps->anchor_conn_event_cntr,
                                                            subrate_factor);

    ble_ll_cs_offset_clamp(ps->offset_min, ps->offset_max,
                           rx_ps->offset_min, rx_ps->offset_max,
                           &ps->offset_min, &ps->offset_max);

    ps->event_offset = ps->offset_min;
    ps->event_interval = round_up_to_subrate_factor(rx_ps->event_interval, ps->event_interval,
                                                    subrate_factor);

    ps->subevent_len = MIN(ps->subevent_len, rx_ps->subevent_len);
    ps->subevents_per_event = MIN(MIN(ps->subevents_per_event, BLE_LL_CS_SUBEVENTS_PER_EVENT_MAX),
                                  rx_ps->subevents_per_event);
    if (ps->subevents_per_event == 1) {
        ps->subevent_interval = 0;
    } else if (ps->subevent_interval == 0) {
        ps->subevents_per_event = 1;
    } else {
        ps->subevent_interval = MAX(ps->subevent_interval, rx_ps->subevent_interval);

        subevent_interval_us = ps->subevent_interval * BLE_LL_CS_SUBEVENTS_INTERVAL_UNIT_US;
        if (subevent_interval_us < ps->subevent_len + BLE_LL_CS_SUBEVENT_MIN_SPACING_US) {
            subevent_interval_us = ps->subevent_len + BLE_LL_CS_SUBEVENT_MIN_SPACING_US;
            ps->subevent_interval = div_ceil(subevent_interval_us,
                                             BLE_LL_CS_SUBEVENTS_INTERVAL_UNIT_US);
        }

        event_interval_us = rx_ps->event_interval * conn_itvl_us;
        if (event_interval_us < subevent_interval_us || ps->max_procedure_len < ps->subevent_interval) {
            ps->subevent_interval = 0;
            ps->subevents_per_event = 1;
        }
    }

    if (rx_ps->aci > 7) {
        /* Resuggest ACI with ID 0 as the controller does not support antenna switching yet */
        rx_ps->aci = 0;
    }
    ps->aci = rx_ps->aci;

    if (rx_ps->phy != 0b0001 && rx_ps->phy != 0b0010) {
        /* Resuggest using LE 1M PHY */
        rx_ps->phy = 1;
    }
    ps->phy = rx_ps->phy;

    if (!(IN_RANGE(rx_ps->tx_power_delta, 0x00, 0x14) ||
          IN_RANGE(rx_ps->tx_power_delta, 0x7E, 0xFF))) {
        /* Invalid range. Resuggest to not change the power level */
        rx_ps->tx_power_delta = 0x00;
    }
    ps->tx_power_delta = rx_ps->tx_power_delta;

    return 0;
}

int
ble_ll_cs_start_ind_parameters_apply(struct ble_ll_cs_proc_params *ps,
                                     struct ble_ll_cs_proc_params *rx_ps, uint32_t conn_itvl_us,
                                     uint32_t ce_duration_us, uint16_t subrate_factor,
                                     uint16_t event_cntr)
{
    uint32_t subevent_interval_us;
    uint32_t event_interval_us;
    uint16_t event_interval;

    event_cntr = round_up_to_subrate_factor(rx_ps->anchor_conn_event_cntr,
                                            ps->anchor_conn_event_cntr,
                                            subrate_factor);
    if (event_cntr != rx_ps->anchor_conn_event_cntr) {
        return 1;
    }
    ps->anchor_conn_event_cntr = event_cntr;

    if (rx_ps->event_offset < ps->offset_min || ps->offset_max < rx_ps->event_offset) {
        return 1;
    }
    ps->event_offset = rx_ps->event_offset;

    event_interval = round_up_to_subrate_factor(rx_ps->event_interval, ps->event_interval,
                                                subrate_factor);
    if (event_interval != rx_ps->event_interval) {
        return 1;
    }
    ps->event_interval = event_interval;

    if (ps->subevent_len < rx_ps->subevent_len) {
        return 1;
    }
    ps->subevent_len = rx_ps->subevent_len;

    if (ps->subevents_per_event < rx_ps->subevents_per_event ||
        BLE_LL_CS_SUBEVENTS_PER_EVENT_MAX < rx_ps->subevents_per_event) {
        return 1;
    }
    ps->subevents_per_event = rx_ps->subevents_per_event;

    if (ps->subevents_per_event == 1) {
        if (ps->subevent_interval != 0) {
            return 1;
        }
    } else if (rx_ps->subevent_interval < ps->subevent_interval) {
        return 1;
    } else if (ps->subevent_interval < rx_ps->subevent_interval) {
        subevent_interval_us = rx_ps->subevent_interval * BLE_LL_CS_SUBEVENTS_INTERVAL_UNIT_US;
        event_interval_us = ps->event_interval * conn_itvl_us;
        if (event_interval_us < subevent_interval_us) {
            return 1;
        }
        ps->subevent_interval = rx_ps->subevent_interval;
    }

    if (ps->aci > 7) {
        return 1;
    }

    if (ps->phy != 0b0001 && ps->phy != 0b0010) {
        return 1;
    }

    if (!(IN_RANGE(ps->tx_power_delta, 0x00, 0x14) ||
          IN_RANGE(ps->tx_power_delta, 0x7E, 0xFF))) {
        return 1;
    }

    return 0;
}

int
ble_ll_cs_rx_cs_start_req(struct ble_ll_conn_sm *connsm, uint8_t *dptr, uint8_t *rspbuf)
{
    uint32_t conn_itvl_us;
    uint32_t ce_duration_us;
    uint16_t subrate_base_event;
    uint16_t subrate_factor;
    uint8_t config_id = *dptr & 0b00111111;
    struct ble_ll_cs_config *conf;
    struct ble_ll_cs_proc_params *ps;
    int rc;

    if (IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CS_START)) {
        if (CONN_IS_CENTRAL(connsm)) {
            /* Reject CS config initiated by peripheral */
            ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_REQ,
                                         BLE_ERR_LMP_COLLISION, rspbuf);
            return BLE_LL_CTRL_REJECT_IND_EXT;
        } else {
            /* Take no further action in the Peripheral-initiated procedure
             * and proceed to handle the Central-initiated procedure.
             */
            ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_START);
        }
    }

    if (config_id >= ARRAY_SIZE(connsm->cssm->config)) {
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_REQ,
                                     BLE_ERR_INV_LMP_LL_PARM, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    conf = &connsm->cssm->config[config_id];
    ps = &conf->proc_params;
    memset(ps, 0, sizeof(*ps));

    if (ble_ll_cs_proc_params_channels_setup(conf)) {
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_REQ,
                                     BLE_ERR_INSUFFICIENT_CHAN, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    ps->anchor_conn_event_cntr = get_le16(dptr + 1);
    ps->offset_min = get_le24(dptr + 3);
    ps->offset_max = get_le24(dptr + 6);
    ps->max_procedure_len = get_le16(dptr + 9);
    ps->event_interval = get_le16(dptr + 11);
    ps->subevents_per_event = dptr[13];
    ps->subevent_interval = get_le16(dptr + 14);
    ps->subevent_len = get_le24(dptr + 16);
    ps->procedure_interval = get_le16(dptr + 19);
    ps->max_procedure_count = get_le16(dptr + 21);
    ps->aci = dptr[23];
    ps->preferred_peer_antenna = dptr[24] & 0b00001111;
    ps->phy = dptr[25] & 0b00001111;
    ps->tx_power_delta = dptr[26];
    ps->tx_snr_i = dptr[27] & 0b00001111;
    ps->tx_snr_r = (dptr[27] >> 4) & 0b00001111;

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_ENHANCED_CONN_UPDATE)
    subrate_factor = connsm->subrate_factor;
    subrate_base_event = connsm->subrate_base_event;
#else
    subrate_factor = 1;
    subrate_base_event = connsm->event_cntr;
#endif
    conn_itvl_us = ble_ll_tmr_t2u(connsm->conn_itvl_ticks);
    ce_duration_us = ble_ll_tmr_t2u(connsm->conn_sch.end_time - connsm->conn_sch.start_time);

    rc = ble_ll_cs_start_req_parameters_apply(ps, conn_itvl_us, ce_duration_us, subrate_base_event,
                                              subrate_factor, connsm->event_cntr);
    if (rc) {
        memset(ps, 0, sizeof(*ps));
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_REQ,
                                     BLE_ERR_INV_LMP_LL_PARM, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    /* In Central role reply with LL_CS_IND instead of LL_CS_RSP */
    if (CONN_IS_CENTRAL(connsm)) {
        rc = ble_ll_cs_proc_scheduling_start(connsm, config_id);
        if (rc) {
            memset(ps, 0, sizeof(*ps));
            ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_REQ, rc, rspbuf);
            return BLE_LL_CTRL_REJECT_IND_EXT;
        }

        ble_ll_cs_ev_cs_proc_enable_complete(connsm, config_id, BLE_ERR_SUCCESS);

        return ble_ll_cs_start_ind_make(connsm, rspbuf);
    }

    connsm->cssm->config_req_id = config_id;

    return ble_ll_cs_start_rsp_make(connsm, rspbuf);
}

int
ble_ll_cs_rx_cs_start_rsp(struct ble_ll_conn_sm *connsm, uint8_t *dptr, uint8_t *rspbuf)
{
    int rc;
    struct ble_ll_cs_config *conf;
    struct ble_ll_cs_proc_params *ps;
    struct ble_ll_cs_proc_params rx_ps;
    uint32_t conn_itvl_us;
    uint32_t ce_duration_us;
    uint16_t subrate_factor;
    uint8_t config_id = *dptr & 0b00111111;

    if (!IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CS_START) ||
        CONN_IS_PERIPHERAL(connsm)) {
        /* Ignore */
        return BLE_ERR_MAX;
    }

    if (config_id != connsm->cssm->config_req_id) {
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_RSP,
                                     BLE_ERR_INV_LMP_LL_PARM, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    conf = &connsm->cssm->config[config_id];
    ps = &conf->proc_params;

    rx_ps.anchor_conn_event_cntr = get_le16(dptr + 1);
    rx_ps.offset_min = get_le24(dptr + 3);
    rx_ps.offset_max = get_le24(dptr + 6);
    rx_ps.event_interval = get_le16(dptr + 9);
    rx_ps.subevents_per_event = dptr[11];
    rx_ps.subevent_interval = get_le16(dptr + 12);
    rx_ps.subevent_len = get_le24(dptr + 14);
    rx_ps.aci = dptr[17];
    rx_ps.phy = dptr[18] & 0b00001111;
    rx_ps.tx_power_delta = dptr[19];

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_ENHANCED_CONN_UPDATE)
    subrate_factor = connsm->subrate_factor;
#else
    subrate_factor = 1;
#endif
    conn_itvl_us = ble_ll_tmr_t2u(connsm->conn_itvl_ticks);
    ce_duration_us = ble_ll_tmr_t2u(connsm->conn_sch.end_time - connsm->conn_sch.start_time);

    rc = ble_ll_cs_start_rsp_parameters_apply(ps, &rx_ps, conn_itvl_us, ce_duration_us,
                                              subrate_factor, connsm->event_cntr);
    if (rc) {
        memset(ps, 0, sizeof(*ps));
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_RSP,
                                     BLE_ERR_INV_LMP_LL_PARM, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    /* Stop the control procedure and send an event to the host */
    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_START);

    rc = ble_ll_cs_proc_scheduling_start(connsm, config_id);
    if (rc) {
        memset(ps, 0, sizeof(*ps));
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_RSP, rc, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    ble_ll_cs_ev_cs_proc_enable_complete(connsm, config_id, BLE_ERR_SUCCESS);

    return ble_ll_cs_start_ind_make(connsm, rspbuf);
}

int
ble_ll_cs_rx_cs_start_ind(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                          uint8_t *rspbuf)
{
    int rc;
    struct ble_ll_cs_config *conf;
    struct ble_ll_cs_proc_params *ps;
    struct ble_ll_cs_proc_params rx_ps;
    uint32_t conn_itvl_us;
    uint32_t ce_duration_us;
    uint16_t subrate_factor;
    uint8_t config_id = *dptr & 0b00111111;

    if (CONN_IS_CENTRAL(connsm)) {
        /* Ignore unexpected response */
        return BLE_ERR_MAX;
    }

    if (config_id != connsm->cssm->config_req_id) {
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_IND,
                                     BLE_ERR_INV_LMP_LL_PARM, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    conf = &connsm->cssm->config[config_id];
    ps = &conf->proc_params;

    /* Overwrite ressugested values */
    rx_ps.anchor_conn_event_cntr = get_le16(dptr + 1);
    rx_ps.event_offset = get_le24(dptr + 3);
    rx_ps.event_interval = get_le16(dptr + 6);
    rx_ps.subevents_per_event = dptr[8];
    rx_ps.subevent_interval = get_le16(dptr + 9);
    rx_ps.subevent_len = get_le24(dptr + 11);
    rx_ps.aci = dptr[14];
    rx_ps.phy = dptr[15];
    rx_ps.tx_power_delta = dptr[16];

#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_ENHANCED_CONN_UPDATE)
    subrate_factor = connsm->subrate_factor;
#else
    subrate_factor = 1;
#endif
    conn_itvl_us = ble_ll_tmr_t2u(connsm->conn_itvl_ticks);
    ce_duration_us = ble_ll_tmr_t2u(connsm->conn_sch.end_time - connsm->conn_sch.start_time);

    rc = ble_ll_cs_start_ind_parameters_apply(ps, &rx_ps, conn_itvl_us, ce_duration_us,
                                              subrate_factor, connsm->event_cntr);
    if (rc) {
        memset(ps, 0, sizeof(*ps));
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_IND,
                                     BLE_ERR_INV_LMP_LL_PARM, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    rc = ble_ll_cs_proc_scheduling_start(connsm, config_id);
    if (rc) {
        memset(ps, 0, sizeof(*ps));
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_IND, rc, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    ble_ll_cs_ev_cs_proc_enable_complete(connsm, config_id, BLE_ERR_SUCCESS);

    return BLE_ERR_MAX;
}

void
ble_ll_cs_rx_cs_start_rejected(struct ble_ll_conn_sm *connsm, uint8_t ble_error)
{
    /* Stop the control procedure and send an event to the host */
    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_START);
    ble_ll_cs_ev_cs_proc_enable_complete(connsm, connsm->cssm->config_req_id, ble_error);
}

void
ble_ll_cs_terminate_req_make(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    struct ble_ll_cs_sm *cssm = connsm->cssm;
    uint8_t config_id = cssm->terminate_config_id;

    assert(config_id < ARRAY_SIZE(cssm->config));

    *dptr = config_id;
    put_le16(dptr + 1, cssm->pending_procedure_id);
    dptr[3] = cssm->terminate_error_code;
}

int
ble_ll_cs_rx_cs_terminate_req(struct ble_ll_conn_sm *connsm, uint8_t *dptr,
                              uint8_t *rspbuf)
{
    struct ble_ll_cs_sm *cssm = connsm->cssm;
    uint8_t config_id = *dptr & 0b00111111;
    uint16_t remote_procedure_id = get_le16(dptr + 1);
    struct ble_ll_cs_proc_params *params;

    if (config_id >= ARRAY_SIZE(cssm->config)) {
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_TERMINATE_REQ,
                                     BLE_ERR_INV_LMP_LL_PARM, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    params = &cssm->config[config_id].proc_params;

    if (params->max_procedure_count != BLE_LL_CS_PROCEDURE_COUNT_NO_LIMIT &&
        remote_procedure_id > cssm->first_procedure_id + params->max_procedure_count) {
        ble_ll_ctrl_rej_ext_ind_make(BLE_LL_CTRL_CS_TERMINATE_REQ,
                                     BLE_ERR_CMD_DISALLOWED, rspbuf);
        return BLE_LL_CTRL_REJECT_IND_EXT;
    }

    if (cssm->pending_procedure_id < remote_procedure_id) {
        cssm->terminate_procedure_id = remote_procedure_id;
    } else {
        cssm->terminate_procedure_id = cssm->pending_procedure_id;
    }
    cssm->terminate_measurement = 1;

    *rspbuf = config_id;
    put_le16(rspbuf + 1, cssm->terminate_procedure_id);
    rspbuf[3] = BLE_ERR_SUCCESS;

    return BLE_LL_CTRL_CS_TERMINATE_RSP;
}

void
ble_ll_cs_rx_cs_terminate_rsp(struct ble_ll_conn_sm *connsm, uint8_t *dptr)
{
    struct ble_ll_cs_sm *cssm = connsm->cssm;
    uint8_t config_id = *dptr & 0b00111111;
    uint8_t error_code = dptr[3];
    uint16_t remote_procedure_id = get_le16(dptr + 1);

    if (config_id != cssm->config_req_id ||
        !IS_PENDING_CTRL_PROC(connsm, BLE_LL_CTRL_PROC_CS_TERMINATE)) {
        return;
    }

    if (cssm->pending_procedure_id < remote_procedure_id) {
        cssm->next_procedure_id = remote_procedure_id + 1;
    } else {
        cssm->next_procedure_id = cssm->pending_procedure_id + 1;
    }

    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_TERMINATE);

    ble_ll_cs_ev_cs_proc_enable_complete(connsm, config_id, error_code);
}

void
ble_ll_cs_rx_cs_terminate_req_rejected(struct ble_ll_conn_sm *connsm, uint8_t ble_error)
{
    ble_ll_ctrl_proc_stop(connsm, BLE_LL_CTRL_PROC_CS_TERMINATE);
    ble_ll_cs_ev_cs_proc_enable_complete(connsm, connsm->cssm->config_req_id, ble_error);
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

    /* Set local CS capabilities. */

    /* Mode-3 supported */
    cap->mode_types = 0x01;
    cap->rtt_capability = 0x00;
    /* XXX: Number of CS_SYNC exchanges needed to satisfy the accuracy requirements.
     * This should be a tested capability claim or 0 if RTT not supported.
     * For now, let's use an arbitrary value for testing.
     */
    cap->rtt_aa_only_n = 30;
    cap->rtt_sounding_n = 30;
    cap->rtt_random_sequence_n = 30;
    cap->nadm_sounding_capability = 0x0000;
    cap->nadm_random_sequence_capability = 0x0000;
    /* LE 2M PHY supported */
    cap->cs_sync_phy_capability = 0b010;
    cap->number_of_antennas = 0x01;
    cap->max_number_of_antenna_paths = 0x01;
    cap->roles_supported = 0x03;
    /* FAE not supported */
    cap->no_fae = 0x01;
    /* CSA #3C supported */
    cap->channel_selection = 0x01;
    cap->sounding_pct_estimate = 0x00;
    cap->max_number_of_configs = 0x04;
    /* Support for both a fixed number of consecutive CS procedures and for an
     * indefinite number of CS procedures until termination.
     */
    cap->max_number_of_procedures = 0x0000;
    /* Antenna switching not supported */
    cap->t_sw = 0x00;
    /* Only mandatory timing supported for now */
    cap->t_ip1_capability = 0;
    cap->t_ip2_capability = 0;
    cap->t_fcs_capability = 0;
    cap->t_pm_capability = 0;
    cap->tx_snr_capablity = 0x00;

    ble_ll_cs_proc_set_chan_class(default_channel_classification);

    ble_ll_cs_proc_init();
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
