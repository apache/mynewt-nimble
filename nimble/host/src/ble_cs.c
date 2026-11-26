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

#include <inttypes.h>
#include "syscfg/syscfg.h"

#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)

#include "host/ble_hs_log.h"
#include "host/ble_hs.h"
#include "host/ble_cs.h"
#include "nimble/hci_common.h"
#include "ble_hs_hci_priv.h"
#include "ble_hs_priv.h"

struct ble_cs_rd_rem_supp_cap_cp {
    uint16_t conn_handle;
} __attribute__((packed));

struct ble_cs_sec_enable_cp {
    uint16_t conn_handle;
} __attribute__((packed));

struct ble_cs_set_def_settings_cp {
    uint16_t conn_handle;
    uint8_t role_enable;
    uint8_t cs_sync_antenna_selection;
    uint8_t max_tx_power;
} __attribute__((packed));
struct ble_cs_set_def_settings_rp {
    uint16_t conn_handle;
} __attribute__((packed));

struct ble_cs_rd_rem_fae_cp {
    uint16_t conn_handle;
} __attribute__((packed));

struct ble_cs_create_config_cp {
    uint16_t conn_handle;
    uint8_t config_id;
    /* If the config should be created on the remote controller too */
    uint8_t create_context;
    /* The main mode to be used in the CS procedures */
    uint8_t main_mode_type;
    /* The sub mode to be used in the CS procedures */
    uint8_t sub_mode_type;
    /* Minimum/maximum number of CS main mode steps to be executed before
     * a submode step.
     */
    uint8_t min_main_mode_steps;
    uint8_t max_main_mode_steps;
    /* The number of main mode steps taken from the end of the last
     * CS subevent to be repeated at the beginning of the current CS subevent
     * directly after the last mode 0 step of that event
     */
    uint8_t main_mode_repetition;
    uint8_t mode_0_steps;
    uint8_t role;
    uint8_t rtt_type;
    uint8_t cs_sync_phy;
    uint8_t channel_map[BLE_CS_CHANNEL_MAP_LEN];
    uint8_t channel_map_repetition;
    uint8_t channel_selection_type;
    uint8_t ch3c_shape;
    uint8_t ch3c_jump;
    /* Was companion_signal_enable in a pre-release draft; the field did not
     * make it into Core 6.0 and the octet is reserved. */
    uint8_t reserved;
} __attribute__((packed));

struct ble_cs_remove_config_cp {
    uint16_t conn_handle;
    uint8_t config_id;
} __attribute__((packed));

struct ble_cs_set_proc_params_cp {
    uint16_t conn_handle;
    uint8_t config_id;
    /* The maximum duration of each CS procedure (time = N x 0.625 ms) */
    uint16_t max_procedure_len;
    /* The minimum and maximum number of connection events between
     * consecutive CS procedures. Ignored if only one CS procedure. */
    uint16_t min_procedure_interval;
    uint16_t max_procedure_interval;
    /* The maximum number of consecutive CS procedures to be scheduled */
    uint16_t max_procedure_count;
    /* Minimum/maximum suggested durations for each CS subevent in
     * microseconds.  Only 3 bytes meaningful. */
    uint32_t min_subevent_len;
    uint32_t max_subevent_len;
    /* Antenna Configuration Index (ACI) for switching during phase
     * measurement */
    uint8_t tone_antenna_config_selection;
    /* The remote device's Tx PHY. A 4 bits bitmap. */
    uint8_t phy;
    /* How many more or fewer of transmit power levels should the remote
     * device's Tx PHY use during the CS tones and RTT transmission */
    uint8_t tx_power_delta;
    /* Preferred peer-ordered antenna elements to be used by the peer for
     * the selected antenna configuration (ACI). A 4 bits bitmap. */
    uint8_t preferred_peer_antenna;
    /* SNR Output Index (SOI) for SNR control adjustment. */
    uint8_t snr_control_initiator;
    uint8_t snr_control_reflector;
} __attribute__((packed));
struct ble_cs_set_proc_params_rp {
    uint16_t conn_handle;
} __attribute__((packed));

struct ble_cs_proc_enable_cp {
    uint16_t conn_handle;
    uint8_t config_id;
    uint8_t enable;
} __attribute__((packed));

/* Optional_Subfeatures_Supported bit 1, Core 6.0 Vol 4, Part E, Section
 * 7.8.135: the peer's mode-0 FAE is zero, so its FAE table must not be
 * read. */
#define BLE_CS_SUBFEATURE_NO_FAE 0x0002

/* cs_state.op: a teardown is in progress and the completion events are driving
 * it forward, see ble_cs_initiator_procedure_terminate(). */
#define BLE_CS_OP_TERMINATING    0x01

static int ble_cs_create_default_config(uint16_t conn_handle);
static void ble_cs_terminate_remove_config(uint16_t conn_handle);
static void ble_cs_terminate_disable_roles(uint16_t conn_handle);

struct ble_cs_state {
    uint8_t op;
    /* Connection the callback below was registered for, so that the state of a
     * connection that goes away is not reused by the next one. */
    uint16_t conn_handle;
    ble_cs_event_fn *cb;
    void *cb_arg;
};

static struct ble_cs_state cs_state = {
    .conn_handle = BLE_HS_CONN_HANDLE_NONE,
};

/* Channel_Map of the configuration the host creates, one bit per CS channel
 * with channel 0 in the LSB of byte 0.  The default leaves the channels the
 * specification does not allow for Channel Sounding cleared and keeps to the
 * lower part of the band; ble_cs_set_channel_map() replaces it.
 */
static uint8_t ble_cs_channel_map[BLE_CS_CHANNEL_MAP_LEN] = {
    0x08, 0xfa, 0x4f, 0xac, 0xfa, 0xc0, 0x00, 0x00, 0x00, 0x00,
};

int
ble_cs_set_channel_map(const uint8_t *channel_map)
{
    if (channel_map == NULL) {
        return BLE_HS_EINVAL;
    }

    ble_hs_lock();
    memcpy(ble_cs_channel_map, channel_map, sizeof(ble_cs_channel_map));
    ble_hs_unlock();

    return 0;
}

static void
ble_cs_state_clear(uint16_t conn_handle)
{
    ble_hs_lock();
    if (cs_state.conn_handle != conn_handle) {
        ble_hs_unlock();
        return;
    }

    cs_state.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    cs_state.op = 0;
    cs_state.cb = NULL;
    cs_state.cb_arg = NULL;
    ble_hs_unlock();
}

static bool
ble_cs_terminate_pending(uint16_t conn_handle)
{
    bool pending;

    /* op is written only by ble_cs_initiator_procedure_terminate() and cleared
     * from three places: ble_cs_state_clear(), at the end of the chain in
     * ble_cs_terminate_complete() and on disconnect via
     * ble_cs_connection_broken(); and the two session entry points,
     * ble_cs_initiator_procedure_start() and ble_cs_reflector_setup().  The
     * entry points are what bound it to a session - a teardown whose disable
     * or remove completes with an error status returns without clearing, so
     * the link alone is too coarse a scope. */
    ble_hs_lock();
    pending = (cs_state.conn_handle == conn_handle &&
               cs_state.op == BLE_CS_OP_TERMINATING);
    ble_hs_unlock();

    return pending;
}

void
ble_cs_connection_broken(uint16_t conn_handle)
{
    /* Core 6.0 Vol 4, Part E, Section 7.7.5: the CS configurations of a
     * connection are deleted along with the connection, so nothing is sent to
     * the controller here.  The host only drops its own state, otherwise the
     * next connection would inherit this one's application callback. */
    ble_cs_state_clear(conn_handle);
}

static int
ble_cs_call_event_cb(struct ble_cs_event *event)
{
    ble_cs_event_fn *cb;
    void *cb_arg;
    int rc;

    /* Snapshot cb, cb_arg, and conn_handle under lock to avoid a mismatched
     * cb/cb_arg pair and to guard against stale events from a prior connection
     * reaching a callback registered for the current one. */
    uint16_t state_conn_handle;

    ble_hs_lock();
    cb = cs_state.cb;
    cb_arg = cs_state.cb_arg;
    state_conn_handle = cs_state.conn_handle;
    ble_hs_unlock();

    /* All event union members share conn_handle as their first field. */
    if (event->procedure_complete.conn_handle != state_conn_handle) {
        return 0;
    }

    if (cb != NULL) {
        rc = cb(event, cb_arg);
    } else {
        rc = 0;
    }

    return rc;
}

static void
ble_cs_call_procedure_complete_cb(uint16_t conn_handle, uint8_t status)
{
    struct ble_cs_event event;

    memset(&event, 0, sizeof event);
    event.type = BLE_CS_EVENT_PROCEDURE_COMPLETE;
    event.procedure_complete.conn_handle = conn_handle;
    event.procedure_complete.status = status;
    ble_cs_call_event_cb(&event);
}

static void
ble_cs_call_result_cb(const struct ble_hci_ev_le_subev_cs_subevent_result *ev,
                      uint16_t steps_len)
{
    struct ble_cs_event event;

    memset(&event, 0, sizeof(event));

    event.type = BLE_CS_EVENT_SUBEVENT_RESULT;
    event.subev_result.conn_handle = le16toh(ev->conn_handle);
    event.subev_result.config_id = ev->config_id;
    event.subev_result.start_acl_conn_event_counter =
        le16toh(ev->start_acl_conn_event_counter);
    event.subev_result.procedure_counter = le16toh(ev->procedure_counter);
    event.subev_result.frequency_compensation =
        le16toh(ev->frequency_compensation);
    event.subev_result.reference_power_level = ev->reference_power_level;
    event.subev_result.procedure_done_status = ev->procedure_done_status;
    event.subev_result.subevent_done_status = ev->subevent_done_status;
    event.subev_result.abort_reason = ev->abort_reason;
    event.subev_result.num_antenna_paths = ev->num_antenna_paths;
    event.subev_result.num_steps_reported = ev->num_steps_reported;

    /* Step data is variable length, so hand out a pointer into the HCI event
     * buffer.  It is only valid for the duration of the callback. */
    event.subev_result.steps = ev->steps;
    event.subev_result.steps_len = steps_len;

    ble_cs_call_event_cb(&event);
}

static void
ble_cs_call_result_continue_cb(
    const struct ble_hci_ev_le_subev_cs_subevent_result_continue *ev,
    uint16_t steps_len)
{
    struct ble_cs_event event;

    memset(&event, 0, sizeof(event));

    event.type = BLE_CS_EVENT_SUBEVENT_RESULT_CONTINUE;
    event.subev_result_continue.conn_handle = le16toh(ev->conn_handle);
    event.subev_result_continue.config_id = ev->config_id;
    event.subev_result_continue.procedure_done_status =
        ev->procedure_done_status;
    event.subev_result_continue.subevent_done_status =
        ev->subevent_done_status;
    event.subev_result_continue.abort_reason = ev->abort_reason;
    event.subev_result_continue.num_antenna_paths = ev->num_antenna_paths;
    event.subev_result_continue.num_steps_reported = ev->num_steps_reported;
    event.subev_result_continue.steps = ev->steps;
    event.subev_result_continue.steps_len = steps_len;

    ble_cs_call_event_cb(&event);
}

int
ble_cs_read_local_supp_cap(struct ble_cs_local_supp_cap *out)
{
    struct ble_hci_le_cs_rd_loc_supp_cap_rp rp;
    int rc;

    if (out == NULL) {
        return BLE_HS_EINVAL;
    }

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_CS_RD_LOC_SUPP_CAP),
                           NULL, 0, &rp, sizeof(rp));
    if (rc != 0) {
        return rc;
    }

    out->num_config_supported = rp.num_config_supported;
    out->max_consecutive_procedures_supported =
        le16toh(rp.max_consecutive_procedures_supported);
    out->num_antennas_supported = rp.num_antennas_supported;
    out->max_antenna_paths_supported = rp.max_antenna_paths_supported;
    out->roles_supported = rp.roles_supported;
    out->optional_modes_supported = rp.optional_modes_supported;
    out->rtt_capability = rp.rtt_capability;
    out->rtt_aa_only_n = rp.rtt_aa_only_n;
    out->rtt_sounding_n = rp.rtt_sounding_n;
    out->rtt_random_payload_n = rp.rtt_random_payload_n;
    out->optional_nadm_sounding_capability =
        le16toh(rp.optional_nadm_sounding_capability);
    out->optional_nadm_random_capability =
        le16toh(rp.optional_nadm_random_capability);
    out->optional_cs_sync_phys_supported = rp.optional_cs_sync_phys_supported;
    out->optional_subfeatures_supported =
        le16toh(rp.optional_subfeatures_supported);
    out->optional_t_ip1_times_supported =
        le16toh(rp.optional_t_ip1_times_supported);
    out->optional_t_ip2_times_supported =
        le16toh(rp.optional_t_ip2_times_supported);
    out->optional_t_fcs_times_supported =
        le16toh(rp.optional_t_fcs_times_supported);
    out->optional_t_pm_times_supported =
        le16toh(rp.optional_t_pm_times_supported);
    out->t_sw_time_supported = rp.t_sw_time_supported;
    out->tx_snr_capability = rp.tx_snr_capability;

    return 0;
}

static int
ble_cs_rd_rem_supp_cap(const struct ble_cs_rd_rem_supp_cap_cp *cmd)
{
    struct ble_hci_le_cs_rd_rem_supp_cap_cp cp;

    cp.conn_handle = htole16(cmd->conn_handle);

    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                        BLE_HCI_OCF_LE_CS_RD_REM_SUPP_CAP),
                             &cp, sizeof(cp), NULL, 0);
}

static int
ble_cs_sec_enable(const struct ble_cs_sec_enable_cp *cmd)
{
    struct ble_hci_le_cs_sec_enable_cp cp;

    cp.conn_handle = htole16(cmd->conn_handle);

    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                        BLE_HCI_OCF_LE_CS_SEC_ENABLE),
                             &cp, sizeof(cp), NULL, 0);
}

static int
ble_cs_set_def_settings(const struct ble_cs_set_def_settings_cp *cmd,
                        struct ble_cs_set_def_settings_rp *rsp)
{
    struct ble_hci_le_cs_set_def_settings_cp cp;
    struct ble_hci_le_cs_set_def_settings_rp rp;
    int rc;

    cp.conn_handle = htole16(cmd->conn_handle);
    cp.role_enable = cmd->role_enable;
    cp.cs_sync_antenna_selection = cmd->cs_sync_antenna_selection;
    cp.max_tx_power = cmd->max_tx_power;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_CS_SET_DEF_SETTINGS),
                           &cp, sizeof(cp), &rp, sizeof(rp));

    if (rc == 0) {
        rsp->conn_handle = le16toh(rp.conn_handle);
    }

    return rc;
}

/* Role_Enable bits of HCI_LE_CS_Set_Default_Settings, Core 6.0 Vol 4, Part E,
 * Section 7.8.131. */
#define BLE_CS_ROLE_ENABLE_INITIATOR 0x01
#define BLE_CS_ROLE_ENABLE_REFLECTOR 0x02

/* Enable the local CS role and program the shared default settings for a
 * connection. These are local commands and need no encryption, so the
 * reflector MUST do this as soon as the connection is established - before
 * pairing and before the initiator begins the over-air CS setup - otherwise
 * the controller may create the config / run security with the reflector role
 * still disabled and silently reject the procedure. */
static int
ble_cs_apply_default_settings(uint16_t conn_handle, uint8_t role_enable)
{
    struct ble_cs_set_def_settings_cp set_cmd;
    struct ble_cs_set_def_settings_rp set_rsp;
    struct ble_gap_conn_desc desc;
    int rc;

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        return rc;
    }

    set_cmd.conn_handle = conn_handle;
    /* Taken from the caller, which knows the CS role it is setting up.  Note
     * that the event handlers in this file key the setup chain on the GAP
     * role - the central drives the initiator side - so the reflector role
     * belongs on links where the local device is the peripheral; a reflector
     * enabled on a central link is overridden as soon as the capability
     * exchange completes. */
    set_cmd.role_enable = role_enable;
    set_cmd.cs_sync_antenna_selection =
        MYNEWT_VAL(BLE_CS_SYNC_ANTENNA_SELECTION);
    set_cmd.max_tx_power = MYNEWT_VAL(BLE_CS_MAX_TX_POWER);

    rc = ble_cs_set_def_settings(&set_cmd, &set_rsp);
    if (rc) {
        BLE_HS_LOG(ERROR, "Failed to set default CS settings conn=%d rc=%d\n",
                   conn_handle, rc);
        return rc;
    }

    BLE_HS_LOG(INFO, "CS default settings configured conn=%d role=%d\n",
               conn_handle, desc.role);
    return 0;
}

/* Disable both local CS roles for a connection. The caller must have removed
 * every CS configuration of the connection first, see
 * ble_cs_initiator_procedure_terminate(). */
static int
ble_cs_disable_roles(uint16_t conn_handle)
{
    struct ble_cs_set_def_settings_cp set_cmd;
    struct ble_cs_set_def_settings_rp set_rsp;

    set_cmd.conn_handle = conn_handle;
    set_cmd.role_enable = 0x00;
    set_cmd.cs_sync_antenna_selection =
        MYNEWT_VAL(BLE_CS_SYNC_ANTENNA_SELECTION);
    set_cmd.max_tx_power = MYNEWT_VAL(BLE_CS_MAX_TX_POWER);

    return ble_cs_set_def_settings(&set_cmd, &set_rsp);
}

static int
ble_cs_rd_rem_fae(const struct ble_cs_rd_rem_fae_cp *cmd)
{
    struct ble_hci_le_cs_rd_rem_fae_cp cp;

    cp.conn_handle = htole16(cmd->conn_handle);

    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                        BLE_HCI_OCF_LE_CS_RD_REM_FAE),
                             &cp, sizeof(cp), NULL, 0);
}

static int
ble_cs_create_config(const struct ble_cs_create_config_cp *cmd)
{
    struct ble_hci_le_cs_create_config_cp cp;

    cp.conn_handle = htole16(cmd->conn_handle);

    cp.config_id = cmd->config_id;
    cp.create_context = cmd->create_context;
    cp.main_mode_type = cmd->main_mode_type;
    cp.sub_mode_type = cmd->sub_mode_type;
    cp.min_main_mode_steps = cmd->min_main_mode_steps;
    cp.max_main_mode_steps = cmd->max_main_mode_steps;
    cp.main_mode_repetition = cmd->main_mode_repetition;
    cp.mode_0_steps = cmd->mode_0_steps;
    cp.role = cmd->role;
    cp.rtt_type = cmd->rtt_type;
    cp.cs_sync_phy = cmd->cs_sync_phy;
    memcpy(cp.channel_map, cmd->channel_map, sizeof(cp.channel_map));
    cp.channel_map_repetition = cmd->channel_map_repetition;
    cp.channel_selection_type = cmd->channel_selection_type;
    cp.ch3c_shape = cmd->ch3c_shape;
    cp.ch3c_jump = cmd->ch3c_jump;
    cp.reserved = 0x00;

    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                        BLE_HCI_OCF_LE_CS_CREATE_CONFIG),
                             &cp, sizeof(cp), NULL, 0);
}

static int
ble_cs_remove_config(const struct ble_cs_remove_config_cp *cmd)
{
    struct ble_hci_le_cs_remove_config_cp cp;

    cp.conn_handle = htole16(cmd->conn_handle);
    cp.config_id = cmd->config_id;

    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                        BLE_HCI_OCF_LE_CS_REMOVE_CONFIG),
                             &cp, sizeof(cp), NULL, 0);
}

static int
ble_cs_set_proc_params(const struct ble_cs_set_proc_params_cp *cmd,
                       struct ble_cs_set_proc_params_rp *rsp)
{
    struct ble_hci_le_cs_set_proc_params_cp cp;
    struct ble_hci_le_cs_set_proc_params_rp rp;
    int rc;

    cp.conn_handle = htole16(cmd->conn_handle);
    cp.config_id = cmd->config_id;
    cp.max_procedure_len = htole16(cmd->max_procedure_len);
    cp.min_procedure_interval = htole16(cmd->min_procedure_interval);
    cp.max_procedure_interval = htole16(cmd->max_procedure_interval);
    cp.max_procedure_count = htole16(cmd->max_procedure_count);
    put_le24(cp.min_subevent_len, cmd->min_subevent_len);
    put_le24(cp.max_subevent_len, cmd->max_subevent_len);
    cp.tone_antenna_config_selection = cmd->tone_antenna_config_selection;
    cp.phy = cmd->phy;
    cp.tx_power_delta = cmd->tx_power_delta;
    cp.preferred_peer_antenna = cmd->preferred_peer_antenna;
    cp.snr_control_initiator = cmd->snr_control_initiator;
    cp.snr_control_reflector = cmd->snr_control_reflector;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_CS_SET_PROC_PARAMS),
                           &cp, sizeof(cp), &rp, sizeof(rp));

    if (rc == 0) {
        rsp->conn_handle = le16toh(rp.conn_handle);
    }

    return rc;
}

static int
ble_cs_proc_enable(const struct ble_cs_proc_enable_cp *cmd)
{
    struct ble_hci_le_cs_proc_enable_cp cp;

    cp.conn_handle = htole16(cmd->conn_handle);
    cp.config_id = cmd->config_id;
    cp.enable = cmd->enable;

    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                        BLE_HCI_OCF_LE_CS_PROC_ENABLE),
                             &cp, sizeof(cp), NULL, 0);
}

/* CS setup step 1: the remote capabilities are known, so the initiator can now
 * program its default settings and read the remote FAE table. */
int
ble_hs_hci_evt_le_cs_rd_rem_supp_cap_complete(uint8_t subevent,
                                              const void *data,
                                              unsigned int len)
{
    int rc = 0;
    const struct ble_hci_ev_le_subev_cs_rd_rem_supp_cap_complete *ev = data;
    struct ble_cs_rd_rem_fae_cp fae_cmd;
    struct ble_gap_conn_desc desc;
    uint16_t conn_handle;

    /* Length is checked before any other field is read: the dispatcher only
     * guarantees that one subevent code byte is present. */
    if (len != sizeof(*ev)) {
        BLE_HS_LOG(ERROR, "CS capabilities exchange: invalid length %u\n", len);
        return BLE_HS_ECONTROLLER;
    }

    conn_handle = le16toh(ev->conn_handle);

    if (ev->status) {
        BLE_HS_LOG(ERROR,
                   "CS capabilities exchange failed conn=%d status=%d\n",
                   conn_handle, ev->status);
        /* The setup chain stops here, so no CS Procedure Enable Complete will
         * arrive.  Report it, otherwise a peer without CS support leaves the
         * application waiting forever. */
        ble_cs_call_procedure_complete_cb(conn_handle, ev->status);
        return BLE_HS_ECONTROLLER;
    }

    BLE_HS_LOG(INFO, "CS capabilities exchanged conn=%d\n", conn_handle);

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ble_cs_call_procedure_complete_cb(conn_handle, BLE_ERR_UNSPECIFIED);
        return rc;
    }

    /* The reflector already programmed its default settings (reflector role
     * enabled) at connection time, before pairing, via ble_cs_reflector_setup.
     * Only the initiator (central) needs to set them here - it drives the CS
     * setup and has no earlier hook - and then read the remote FAE table
     * before creating the config. A reflector must NOT read the initiator's
     * FAE; doing so can confuse the remote initiator's CS state machine. */
    if (desc.role == BLE_GAP_ROLE_MASTER) {
        rc = ble_cs_apply_default_settings(conn_handle,
                                           BLE_CS_ROLE_ENABLE_INITIATOR);
        if (rc) {
            ble_cs_call_procedure_complete_cb(conn_handle, BLE_ERR_UNSPECIFIED);
            return rc;
        }

        /* Core 6.0 Vol 6, Part B, Section 5.1.29: the Mode-0 FAE Table Request
         * procedure "shall only be used when the peer's No_FAE bit is set to
         * 0", and the Link Layer must not send LL_CS_FAE_REQ to a peer that
         * indicated zero FAE.  Such a table is implicitly all-zero, so skip
         * the read and create the config directly. */
        if (le16toh(ev->optional_subfeatures_supported) &
            BLE_CS_SUBFEATURE_NO_FAE) {
            BLE_HS_LOG(INFO,
                       "CS peer reports No_FAE conn=%d, skipping FAE read\n",
                       conn_handle);
            return ble_cs_create_default_config(conn_handle);
        }

        fae_cmd.conn_handle = conn_handle;
        rc = ble_cs_rd_rem_fae(&fae_cmd);
        if (rc == BLE_HS_HCI_ERR(BLE_ERR_UNSUPPORTED)) {
            /* A controller following the Core 6.2 wording of Vol 4, Part E,
             * Section 7.8.135 rejects the command in Command Status instead of
             * completing it with status 0x11.  Same meaning as the No_FAE case
             * handled in the FAE complete handler, so continue the setup. */
            BLE_HS_LOG(INFO, "CS FAE read unsupported conn=%d, continuing\n",
                       conn_handle);
            return ble_cs_create_default_config(conn_handle);
        }
        if (rc) {
            BLE_HS_LOG(ERROR, "Failed to read FAE table conn=%d rc=%d\n",
                       conn_handle, rc);
            ble_cs_call_procedure_complete_cb(conn_handle, BLE_ERR_UNSPECIFIED);
        }
    }

    return rc;
}

/* CS setup step 2: the initiator creates the CS config on both controllers.
 * The config_complete event continues the chain. */
static int
ble_cs_create_default_config(uint16_t conn_handle)
{
    struct ble_cs_create_config_cp cmd;
    struct ble_gap_conn_desc desc;
    int rc;

    memset(&cmd, 0, sizeof(cmd));

    cmd.conn_handle = conn_handle;
    /* The config will use ID 0x00 */
    cmd.config_id = 0x00;
    /* Create the config on the remote controller too */
    cmd.create_context = 0x01;
    cmd.main_mode_type = MYNEWT_VAL(BLE_CS_MAIN_MODE_TYPE);
    cmd.sub_mode_type = MYNEWT_VAL(BLE_CS_SUB_MODE_TYPE);

    /* Range from which the number of CS main mode steps to execute
     * will be randomly selected.
     */
    cmd.min_main_mode_steps = MYNEWT_VAL(BLE_CS_MIN_MAIN_MODE_STEPS);
    cmd.max_main_mode_steps = MYNEWT_VAL(BLE_CS_MAX_MAIN_MODE_STEPS);
    /* The number of main mode steps to be repeated at the beginning of
     * the current CS subevent, irrespective of whether there are some
     * overlapping main mode steps from the previous CS subevent or not.
     */
    cmd.main_mode_repetition = MYNEWT_VAL(BLE_CS_MAIN_MODE_REPETITION);
    /* Number of CS mode 0 steps to be included at the beginning of
     * each CS subevent
     */
    cmd.mode_0_steps = MYNEWT_VAL(BLE_CS_MODE_0_STEPS);

    rc = ble_gap_conn_find(cmd.conn_handle, &desc);
    if (rc != 0) {
        ble_cs_call_procedure_complete_cb(cmd.conn_handle, BLE_ERR_UNSPECIFIED);
        return rc;
    }

    /* The ACL central takes the CS Initiator role, the peripheral the
     * Reflector role.
     */
    if (desc.role == BLE_GAP_ROLE_MASTER) {
        cmd.role = 0x00;
    } else {
        cmd.role = 0x01;
    }

    cmd.rtt_type = MYNEWT_VAL(BLE_CS_RTT_TYPE);
    cmd.cs_sync_phy = MYNEWT_VAL(BLE_CS_SYNC_PHY);

    /* ble_cs_set_channel_map() writes the map under ble_hs_lock, so the copy
     * has to take it too for a consistent snapshot. */
    ble_hs_lock();
    memcpy(cmd.channel_map, ble_cs_channel_map, sizeof(cmd.channel_map));
    ble_hs_unlock();

    cmd.channel_map_repetition = MYNEWT_VAL(BLE_CS_CHANNEL_MAP_REPETITION);
    cmd.channel_selection_type = MYNEWT_VAL(BLE_CS_CHANNEL_SELECTION_TYPE);
    /* Used only by channel selection algorithm #3c. */
    cmd.ch3c_shape = MYNEWT_VAL(BLE_CS_CH3C_SHAPE);
    cmd.ch3c_jump = MYNEWT_VAL(BLE_CS_CH3C_JUMP);

    /* create_context above tells the controller to create the same config on
     * the remote side, so only the initiator issues the command.
     */
    if (desc.role == BLE_GAP_ROLE_MASTER) {
        rc = ble_cs_create_config(&cmd);
        if (rc) {
            BLE_HS_LOG(ERROR, "CS config create failed conn=%d rc=%d\n",
                       conn_handle, rc);
            ble_cs_call_procedure_complete_cb(conn_handle, BLE_ERR_UNSPECIFIED);
        }
    }
    return rc;
}

int
ble_hs_hci_evt_le_cs_rd_rem_fae_complete(uint8_t subevent, const void *data,
                                         unsigned int len)
{
    const struct ble_hci_ev_le_subev_cs_rd_rem_fae_complete *ev = data;
    uint16_t conn_handle_fae;

    /* Length is checked before any other field is read: the dispatcher only
     * guarantees that one subevent code byte is present. */
    if (len != sizeof(*ev)) {
        BLE_HS_LOG(ERROR, "CS FAE complete: invalid length %u\n", len);
        return BLE_HS_ECONTROLLER;
    }

    conn_handle_fae = le16toh(ev->conn_handle);

    /* Per Core 6.0 Vol 6, Part B, Section 5.1.26 the mode-0 FAE table -
     * fetched by the Mode-0 FAE Table Request procedure of Section 5.1.29 - is
     * mandatory before starting a CS procedure.  The one legitimate exception
     * is error 0x11 (BLE_ERR_UNSUPPORTED) which signals that the remote
     * controller has the "No_FAE" bit set - its mode-0 FAE table is implicitly
     * all-zero and CS can proceed.  Any other non-zero status means the FAE
     * table is genuinely unknown and we must not create the CS config. */
    if (ev->status != 0 && ev->status != BLE_ERR_UNSUPPORTED) {
        BLE_HS_LOG(ERROR,
                   "CS FAE read failed status=0x%02x - cannot create config\n",
                   ev->status);
        /* The setup chain stops here, so report it - see the capabilities
         * complete handler for why. */
        ble_cs_call_procedure_complete_cb(conn_handle_fae, ev->status);
        return BLE_HS_ECONTROLLER;
    }

    return ble_cs_create_default_config(conn_handle_fae);
}

/* At 1 the procedure-length calculation below yields 0 and at 0 it underflows;
 * either way max_procedure_len falls outside the 0x0001..0xFFFF range the
 * controller accepts. */
#if MYNEWT_VAL(BLE_CS_PROCEDURE_INTERVAL) < 2 ||                              \
    MYNEWT_VAL(BLE_CS_PROCEDURE_INTERVAL) > 0xFFFF
#error "BLE_CS_PROCEDURE_INTERVAL must be in the range 2..0xFFFF"
#endif

/* Program the CS procedure parameters on the local controller. Shared by both
 * roles but with role-specific scheduling values.
 * config_id must be the ID of a configuration that exists on this link: Core
 * 6.0 Vol 4, Part E, Section 7.8.140 makes the controller reject the command
 * with Invalid HCI Command Parameters otherwise.  The initiator creates its
 * own configuration and so knows the ID; the reflector learns it from
 * LE CS Config Complete and cannot assume 0.
 * conn_itvl is the ACL connection interval in 1.25 ms units. */
static int
ble_cs_set_default_proc_params(uint16_t conn_handle, uint8_t config_id,
                               bool initiator, uint16_t conn_itvl)
{
    struct ble_cs_set_proc_params_cp cmd;
    struct ble_cs_set_proc_params_rp rsp;
    uint32_t proc_len;
    int rc;

    cmd.conn_handle = conn_handle;
    cmd.config_id = config_id;
    if (initiator) {
        /* Pin the procedure interval and cap the procedure length so each
         * procedure ends at least one connection event before the next one
         * starts (procedure length is in
         * 0.625 ms units, conn_itvl in 1.25 ms units, hence the x2). The
         * resulting idle gap lets the RAS server stream a procedure's
         * ranging data before the next procedure's subevent results start
         * flowing - without it, back-to-back procedures collide with the
         * notification stream. */
        proc_len = (uint32_t)conn_itvl * 2 *
                   (MYNEWT_VAL(BLE_CS_PROCEDURE_INTERVAL) - 1);
        cmd.max_procedure_len = proc_len > UINT16_MAX ? UINT16_MAX : proc_len;
        cmd.min_procedure_interval = MYNEWT_VAL(BLE_CS_PROCEDURE_INTERVAL);
        cmd.max_procedure_interval = MYNEWT_VAL(BLE_CS_PROCEDURE_INTERVAL);
        cmd.min_subevent_len = MYNEWT_VAL(BLE_CS_SUBEVENT_LEN);
        cmd.max_subevent_len = MYNEWT_VAL(BLE_CS_SUBEVENT_LEN);
    } else {
        /* Reflector: stay permissive - the remote initiator drives the
         * schedule, and narrower local constraints can make the controller
         * silently refuse the over-the-air Procedure Enable handshake. */
        cmd.max_procedure_len = MYNEWT_VAL(BLE_CS_REFLECTOR_MAX_PROCEDURE_LEN);
        cmd.min_procedure_interval =
            MYNEWT_VAL(BLE_CS_REFLECTOR_MIN_PROCEDURE_INTERVAL);
        cmd.max_procedure_interval =
            MYNEWT_VAL(BLE_CS_REFLECTOR_MAX_PROCEDURE_INTERVAL);
        cmd.min_subevent_len = MYNEWT_VAL(BLE_CS_REFLECTOR_MIN_SUBEVENT_LEN);
        cmd.max_subevent_len = MYNEWT_VAL(BLE_CS_REFLECTOR_MAX_SUBEVENT_LEN);
    }
    /* 0 = no limit: a single Procedure Enable starts a free-running stream of
     * procedures that repeats until disabled (no per-procedure restarts). */
    cmd.max_procedure_count = MYNEWT_VAL(BLE_CS_MAX_PROCEDURE_COUNT);
    cmd.tone_antenna_config_selection =
        MYNEWT_VAL(BLE_CS_TONE_ANTENNA_CONFIG_SELECTION);
    cmd.phy = MYNEWT_VAL(BLE_CS_PROCEDURE_PHY);
    cmd.tx_power_delta = MYNEWT_VAL(BLE_CS_TX_POWER_DELTA);
    cmd.preferred_peer_antenna = MYNEWT_VAL(BLE_CS_PREFERRED_PEER_ANTENNA);
    cmd.snr_control_initiator = MYNEWT_VAL(BLE_CS_SNR_CONTROL_INITIATOR);
    cmd.snr_control_reflector = MYNEWT_VAL(BLE_CS_SNR_CONTROL_REFLECTOR);

    rc = ble_cs_set_proc_params(&cmd, &rsp);
    if (rc) {
        BLE_HS_LOG(ERROR, "CS set procedure params failed conn=%d rc=%d\n",
                   conn_handle, rc);
        return rc;
    }
    BLE_HS_LOG(INFO, "CS procedure params set conn=%d\n", conn_handle);

    return 0;
}

/* CS setup step 4: CS security is up, so the initiator programs the procedure
 * parameters and enables the procedure.  This is the last setup step.
 */
int
ble_hs_hci_evt_le_cs_sec_enable_complete(uint8_t subevent, const void *data,
                                         unsigned int len)
{
    int rc;
    struct ble_cs_proc_enable_cp enable_cmd;
    const struct ble_hci_ev_le_subev_cs_sec_enable_complete *ev = data;
    struct ble_gap_conn_desc desc;
    uint16_t conn_handle;

    /* Length is checked before any other field is read: the dispatcher only
     * guarantees that one subevent code byte is present. */
    if (len != sizeof(*ev)) {
        BLE_HS_LOG(ERROR, "CS security enable: invalid length %u\n", len);
        return BLE_HS_ECONTROLLER;
    }

    conn_handle = le16toh(ev->conn_handle);

    if (ev->status) {
        BLE_HS_LOG(ERROR, "CS security enable failed conn=%d status=%d\n",
                   conn_handle, ev->status);
        /* The setup chain stops here, so report it - see the capabilities
         * complete handler for why. */
        ble_cs_call_procedure_complete_cb(conn_handle, ev->status);
        return BLE_HS_ECONTROLLER;
    }

    BLE_HS_LOG(INFO, "CS security enabled conn=%d\n", conn_handle);

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ble_cs_call_procedure_complete_cb(conn_handle, BLE_ERR_UNSPECIFIED);
        return rc;
    }

    /* The reflector (peripheral) already programmed its procedure parameters
     * right after the config was created (see config_complete), so the
     * controller is ready by the time the initiator issues CS Procedure
     * Enable. Only the initiator (central) programs the parameters here and
     * then enables the procedure. */
    if (desc.role != BLE_GAP_ROLE_MASTER) {
        return 0;
    }

    /* If either command fails to leave the host, no CS Procedure Enable
     * Complete event will ever arrive, so notify here the same way
     * config_complete does for its own dispatch failure - otherwise the
     * application never learns that CS did not start. */
    /* The initiator created this configuration itself with ID 0x00 (see
     * ble_cs_initiator_procedure_start()), which is also the ID it enables
     * below. */
    rc = ble_cs_set_default_proc_params(conn_handle, 0x00, true,
                                        desc.conn_itvl);
    if (rc) {
        ble_cs_call_procedure_complete_cb(conn_handle, BLE_ERR_UNSPECIFIED);
        return rc;
    }

    enable_cmd.conn_handle = conn_handle;
    enable_cmd.config_id = 0x00;
    enable_cmd.enable = 0x01;

    rc = ble_cs_proc_enable(&enable_cmd);
    if (rc) {
        BLE_HS_LOG(ERROR, "CS procedure enable failed conn=%d rc=%d\n",
                   conn_handle, rc);
        ble_cs_call_procedure_complete_cb(conn_handle, BLE_ERR_UNSPECIFIED);
    } else {
        BLE_HS_LOG(INFO, "CS procedure enabled conn=%d\n", conn_handle);
    }
    return rc;
}

/* CS setup step 3: the config exists on both controllers.  The initiator
 * starts the CS security handshake, the reflector programs its procedure
 * parameters.
 */
int
ble_hs_hci_evt_le_cs_config_complete(uint8_t subevent, const void *data,
                                     unsigned int len)
{
    int rc;
    struct ble_gap_conn_desc desc;
    const struct ble_hci_ev_le_subev_cs_config_complete *ev = data;
    struct ble_cs_sec_enable_cp cmd;
    uint16_t conn_handle;

    /* Length is checked before any other field is read: the dispatcher only
     * guarantees that one subevent code byte is present. */
    if (len != sizeof(*ev)) {
        BLE_HS_LOG(ERROR, "CS config complete: invalid length %u\n", len);
        return BLE_HS_ECONTROLLER;
    }

    conn_handle = le16toh(ev->conn_handle);

    if (ev->status) {
        BLE_HS_LOG(ERROR, "CS config create failed conn=%d status=%d\n",
                   conn_handle, ev->status);
        /* The setup chain stops here, so report it - see the capabilities
         * complete handler for why.  The teardown chain is not advanced from
         * this path: Core 6.0 Vol 4, Part E, Section 7.7.65.39 leaves the
         * remaining parameters, Action included, undefined when Status is
         * non-zero, so a failed create and a failed remove cannot be told
         * apart here.  cs_state, including op, is reset on disconnect by
         * ble_cs_connection_broken() -> ble_cs_state_clear(). */
        ble_cs_call_procedure_complete_cb(conn_handle, ev->status);
        return BLE_HS_ECONTROLLER;
    }

    /* Action 0x00 means the configuration was removed (either side may remove
     * one), in which case the remaining event parameters are meaningless and
     * there is nothing to set up. */
    if (ev->action != 0x01) {
        BLE_HS_LOG(WARN, "CS config removed conn=%d config_id=%d action=%d\n",
                   conn_handle, ev->config_id, ev->action);
        /* Last step of the teardown chain started by
         * ble_cs_initiator_procedure_terminate(). */
        if (ble_cs_terminate_pending(conn_handle)) {
            ble_cs_terminate_disable_roles(conn_handle);
        }
        return 0;
    }

    BLE_HS_LOG(INFO, "CS config created conn=%d config_id=%d\n", conn_handle,
               ev->config_id);

    cmd.conn_handle = conn_handle;

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ble_cs_call_procedure_complete_cb(conn_handle, BLE_ERR_UNSPECIFIED);
        return rc;
    }

    if (desc.role == BLE_GAP_ROLE_MASTER) {
        rc = ble_cs_sec_enable(&cmd);
        if (rc) {
            BLE_HS_LOG(ERROR,
                       "CS security enable start failed conn=%d rc=%d\n",
                       conn_handle, rc);
            ble_cs_call_procedure_complete_cb(conn_handle, BLE_ERR_UNSPECIFIED);
            return rc;
        }
    } else {
        /* Reflector: program the procedure parameters now - after the config
         * is created and before the initiator enables the procedure - so the
         * controller is ready when CS Procedure Enable arrives.  A failure
         * here is not reported through the procedure complete callback the way
         * the initiator branches above do: the initiator owns the enable, so
         * the reflector still receives CS Procedure Enable Complete and
         * reports the controller's own status from there. */
        rc = ble_cs_set_default_proc_params(conn_handle, ev->config_id, false,
                                            desc.conn_itvl);
    }

    return rc;
}

/* CS setup is done once this arrives: procedures now run free until disabled,
 * and their results are delivered by the subevent result events below.
 */
int
ble_hs_hci_evt_le_cs_proc_enable_complete(uint8_t subevent, const void *data,
                                          unsigned int len)
{
    const struct ble_hci_ev_le_subev_cs_proc_enable_complete *ev = data;
    struct ble_cs_event event;

    if (len != sizeof(*ev)) {
        BLE_HS_LOG(ERROR, "CS procedure enable complete bad length %u\n", len);
        return BLE_HS_ECONTROLLER;
    }

    if (ev->status) {
        BLE_HS_LOG(ERROR,
                   "CS procedure enable complete failed conn=%d status=%d\n",
                   le16toh(ev->conn_handle), ev->status);
        /* The return value is discarded by the event dispatcher, so this
         * callback is the only way the application learns that CS never
         * started. */
        ble_cs_call_procedure_complete_cb(le16toh(ev->conn_handle), ev->status);
        return BLE_HS_ECONTROLLER;
    }

    /* State 0x00 is a successful disable (application terminate, or the
     * controller reaching max_procedure_count), not an enable. */
    BLE_HS_LOG(INFO, "CS procedure %s conn=%d config_id=%d tx_power=%d\n",
               ev->state ? "enabled" : "disabled", le16toh(ev->conn_handle),
               ev->config_id, (int8_t)ev->selected_tx_power);

    memset(&event, 0, sizeof(event));
    event.type = BLE_CS_EVENT_PROCEDURE_ENABLE_COMPLETE;
    event.procedure_enable_complete.conn_handle = le16toh(ev->conn_handle);
    event.procedure_enable_complete.status = ev->status;
    event.procedure_enable_complete.config_id = ev->config_id;
    event.procedure_enable_complete.selected_tx_power =
        (int8_t)ev->selected_tx_power;
    event.procedure_enable_complete.state = ev->state;
    ble_cs_call_event_cb(&event);

    /* Core 6.0 Vol 4, Part E, Section 7.8.141: for a disable this event only
     * arrives once the pending subevent results have been delivered and
     * LL_CS_TERMINATE_RSP has been exchanged, so this is the first point at
     * which the configuration may be removed. */
    if (ev->state == 0x00 &&
        ble_cs_terminate_pending(le16toh(ev->conn_handle))) {
        ble_cs_terminate_remove_config(le16toh(ev->conn_handle));
    }

    return 0;
}

int
ble_hs_hci_evt_le_cs_subevent_result(uint8_t subevent, const void *data,
                                     unsigned int len)
{
    const struct ble_hci_ev_le_subev_cs_subevent_result *event = data;
    int expected_len = 0;
    void *step_ptr = NULL;
    int steps_remaining = 0;
    int step_size = 0;
    uint8_t *data_end;

    /* The fixed part must be present before any field is read: the dispatcher
     * only guarantees that one subevent code byte is present.  The variable
     * step data is bounds checked in the walk below. */
    if (len < sizeof(*event)) {
        BLE_HS_LOG(ERROR, "cs_subevent_result: short event len=%u\n", len);
        return BLE_HS_ECONTROLLER;
    }

    BLE_HS_LOG(INFO, "CS subevent result conn=%d steps=%d len=%u\n",
               le16toh(event->conn_handle), event->num_steps_reported, len);

    expected_len += sizeof(*event);
    steps_remaining = event->num_steps_reported;
    step_ptr = (void *)event->steps;

    data_end = (uint8_t *)data + len;

    /* Each reported step carries its own data_len, so the total event length
     * can only be verified by walking the steps.  Every read is bounds checked
     * first because data_len comes straight off the transport.
     */
    while (steps_remaining > 0) {
        if ((uint8_t *)step_ptr + sizeof(struct cs_steps_data) > data_end) {
            BLE_HS_LOG(ERROR, "cs_subevent_result: invalid step_ptr bounds\n");
            return BLE_HS_ECONTROLLER;
        }
        step_size = sizeof(struct cs_steps_data) +
                    ((struct cs_steps_data *)step_ptr)->data_len;
        if ((uint8_t *)step_ptr + step_size > data_end) {
            BLE_HS_LOG(ERROR, "cs_subevent_result: invalid step_size\n");
            return BLE_HS_ECONTROLLER;
        }

        expected_len += step_size;
        step_ptr = (uint8_t *)step_ptr + step_size;
        steps_remaining--;
    }

    if (len != expected_len) {
        BLE_HS_LOG(ERROR, "cs_subevent_result: len=%u expected=%d\n", len,
                   expected_len);
        return BLE_HS_ECONTROLLER;
    }

    ble_cs_call_result_cb(event, len - sizeof(*event));
    return 0;
}

int
ble_hs_hci_evt_le_cs_subevent_result_continue(uint8_t subevent,
                                              const void *data,
                                              unsigned int len)
{
    const struct ble_hci_ev_le_subev_cs_subevent_result_continue *event = data;
    int expected_len = 0;
    int steps_remaining = 0;
    void *step_ptr = NULL;
    int step_size = 0;
    uint8_t *data_end;

    /* Same length precondition as ble_hs_hci_evt_le_cs_subevent_result. */
    if (len < sizeof(*event)) {
        BLE_HS_LOG(ERROR, "cs_subevent_continue: short event len=%u\n", len);
        return BLE_HS_ECONTROLLER;
    }

    BLE_HS_LOG(INFO, "CS subevent result continue conn=%d steps=%d len=%u\n",
               le16toh(event->conn_handle), event->num_steps_reported, len);

    expected_len = sizeof(*event);
    steps_remaining = event->num_steps_reported;
    step_ptr = (void *)event->steps;
    data_end = (uint8_t *)data + len;

    /* Same variable-length step walk as
     * ble_hs_hci_evt_le_cs_subevent_result. */
    while (steps_remaining > 0) {
        if ((uint8_t *)step_ptr + sizeof(struct cs_steps_data) > data_end) {
            BLE_HS_LOG(ERROR,
                       "cs_subevent_continue: invalid step_ptr bounds\n");
            return BLE_HS_ECONTROLLER;
        }
        step_size = sizeof(struct cs_steps_data) +
                    ((struct cs_steps_data *)step_ptr)->data_len;
        if ((uint8_t *)step_ptr + step_size > data_end) {
            BLE_HS_LOG(ERROR, "cs_subevent_continue: invalid step_size\n");
            return BLE_HS_ECONTROLLER;
        }

        expected_len += step_size;
        step_ptr = (uint8_t *)step_ptr + step_size;
        steps_remaining--;
    }

    if (len != expected_len) {
        BLE_HS_LOG(ERROR, "cs_subevent_continue: len=%u expected=%d\n", len,
                   expected_len);
        return BLE_HS_ECONTROLLER;
    }
    ble_cs_call_result_continue_cb(event, len - sizeof(*event));
    return 0;
}

int
ble_hs_hci_evt_le_cs_test_end_complete(uint8_t subevent, const void *data,
                                       unsigned int len)
{
    const struct ble_hci_ev_le_subev_cs_test_end_complete *ev = data;

    if (len != sizeof(*ev)) {
        BLE_HS_LOG(ERROR, "CS test end complete: invalid length\n");
        return BLE_HS_ECONTROLLER;
    }

    return 0;
}

/* Only the first command of the CS setup is issued here.  The remaining
 * steps are chained off the HCI events they complete with, in this order:
 * read remote capabilities, read the remote mode-0 FAE table, create the CS
 * config, run CS security start, set the procedure parameters and enable the
 * procedure.
 */
int
ble_cs_initiator_procedure_start(
    const struct ble_cs_initiator_procedure_start_params *params)
{
    struct ble_cs_rd_rem_supp_cap_cp cmd;
    struct ble_gap_conn_desc desc;
    int rc;

    if (params == NULL) {
        return BLE_HS_EINVAL;
    }

    rc = ble_gap_conn_find(params->conn_handle, &desc);
    if (rc != 0) {
        return rc;
    }

    /* The CS control PDUs may only be exchanged over an encrypted ACL, so
     * the controller fails the security-start step with Insufficient
     * Security on an unencrypted link.  Reject here instead of several
     * commands into the chain, where the only symptom is a mid-chain HCI
     * error.  Encryption is the only requirement; the CS DRBG is keyed from
     * the vectors exchanged in LL_CS_SEC_REQ/RSP rather than from the LTK,
     * so Legacy Pairing is fine. */
    if (!desc.sec_state.encrypted) {
        BLE_HS_LOG(ERROR, "CS requires an encrypted link conn=%d\n",
                   params->conn_handle);
        return BLE_HS_EENCRYPT;
    }

    ble_hs_lock();
    cs_state.conn_handle = params->conn_handle;
    /* A teardown that failed part way through leaves op set until the link
     * drops, so a new session on the same connection has to clear it.  Left
     * behind, ble_cs_terminate_pending() would read this session's first
     * ordinary procedure disable as the continuation of that teardown. */
    cs_state.op = 0;
    cs_state.cb = params->cb;
    cs_state.cb_arg = params->cb_arg;
    ble_hs_unlock();

    cmd.conn_handle = params->conn_handle;
    rc = ble_cs_rd_rem_supp_cap(&cmd);
    if (rc) {
        BLE_HS_LOG(DEBUG,
                   "Failed to read remote supported CS capabilities, "
                   "err %d",
                   rc);
        /* The setup chain never starts, so drop the callback registered above
         * instead of leaving it to receive events from a later procedure. */
        ble_cs_state_clear(params->conn_handle);
    }

    return rc;
}

/* Ends a teardown.  The session is retired first and the application is told
 * afterwards, so that a new session the callback starts on the same link is
 * left alone: ble_cs_state_clear() matches on conn_handle only, so clearing
 * after the callback would erase the handle and callback that call had just
 * registered and every event of the new session would then be dropped. */
static void
ble_cs_terminate_complete(uint16_t conn_handle, uint8_t status)
{
    struct ble_cs_event event;
    ble_cs_event_fn *cb = NULL;
    void *cb_arg = NULL;

    {
        ble_hs_lock();
        if (cs_state.conn_handle == conn_handle) {
            cb = cs_state.cb;
            cb_arg = cs_state.cb_arg;
            cs_state.conn_handle = BLE_HS_CONN_HANDLE_NONE;
            cs_state.op = 0;
            cs_state.cb = NULL;
            cs_state.cb_arg = NULL;
        }
        ble_hs_unlock();
    }

    /* No callback means the state had already moved on - a disconnect, or a
     * session for another link - and the event belongs to nobody, which is the
     * same outcome ble_cs_call_event_cb() reaches by comparing conn_handle. */
    if (cb == NULL) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.type = BLE_CS_EVENT_PROCEDURE_COMPLETE;
    event.procedure_complete.conn_handle = conn_handle;
    event.procedure_complete.status = status;
    cb(&event, cb_arg);
}

/* Second step of the teardown chain, entered once the controller reports the
 * procedure disabled.  Core 6.0 Vol 4, Part E, Section 7.8.138 rejects
 * HCI_LE_CS_Remove_Config with Command Disallowed while any CS procedure is
 * still enabled, which is why this cannot run from procedure_terminate(). */
static void
ble_cs_terminate_remove_config(uint16_t conn_handle)
{
    struct ble_cs_remove_config_cp rm_cmd;
    int rc;

    rm_cmd.conn_handle = conn_handle;
    rm_cmd.config_id = 0x00;

    rc = ble_cs_remove_config(&rm_cmd);
    if (rc) {
        BLE_HS_LOG(ERROR, "Failed to remove CS config conn=%d err %d\n",
                   conn_handle, rc);
        ble_cs_terminate_complete(conn_handle, BLE_ERR_UNSPECIFIED);
        return;
    }
    /* Continues in ble_hs_hci_evt_le_cs_config_complete() on action 0x00. */
}

/* Final step: with no configuration left the roles may be disabled, which
 * Core 6.0 Vol 4, Part E, Section 7.8.134 rejects while one still exists. */
static void
ble_cs_terminate_disable_roles(uint16_t conn_handle)
{
    int rc = ble_cs_disable_roles(conn_handle);

    if (rc) {
        BLE_HS_LOG(ERROR, "Failed to disable CS roles conn=%d err %d\n",
                   conn_handle, rc);
    }
    ble_cs_terminate_complete(conn_handle,
                              rc ? BLE_ERR_UNSPECIFIED : BLE_ERR_SUCCESS);
}

/* Undo ble_cs_initiator_procedure_start().  Each controller step has to
 * complete over the air before the next one is allowed, so only the procedure
 * disable is issued here and the rest of the chain runs from the completion
 * events - see the two helpers above.  A zero return therefore means the
 * teardown started; the outcome arrives through the procedure complete
 * callback registered at start time. */
int
ble_cs_initiator_procedure_terminate(uint16_t conn_handle)
{
    struct ble_cs_proc_enable_cp en_cmd;
    int rc;

    en_cmd.conn_handle = conn_handle;
    en_cmd.config_id = 0x00;
    en_cmd.enable = 0x00;

    /* Without a session for this handle the completion events could not
     * carry the chain forward - ble_cs_terminate_pending() would never hold -
     * and no procedure complete callback would ever fire, so refuse to start
     * a teardown that cannot finish. */
    ble_hs_lock();
    if (cs_state.conn_handle != conn_handle) {
        ble_hs_unlock();
        return BLE_HS_ENOENT;
    }
    cs_state.op = BLE_CS_OP_TERMINATING;
    ble_hs_unlock();

    rc = ble_cs_proc_enable(&en_cmd);
    if (rc == BLE_HS_HCI_ERR(BLE_ERR_CMD_DISALLOWED)) {
        /* No procedure was running, so there is nothing to wait for and the
         * configuration can be removed immediately. */
        ble_cs_terminate_remove_config(conn_handle);
        return 0;
    }
    if (rc) {
        BLE_HS_LOG(ERROR, "Failed to disable CS procedure conn=%d err %d\n",
                   conn_handle, rc);
        /* No completion event will arrive to carry the chain forward, so the
         * teardown has to be disarmed - but only the teardown.  The rest of
         * the session stays: the later steps run from the completion handlers
         * and reach ble_cs_terminate_remove_config() and the role disable only
         * while ble_cs_terminate_pending() holds, which needs conn_handle to
         * still match.  Clearing that too would leave a retry unable to re-arm
         * op, so the retry would fail with BLE_HS_ENOENT with the
         * configuration and roles left enabled and the procedure complete
         * callback never called, and events for a procedure that may still
         * be running would be dropped.  Full teardown belongs to the end of
         * the chain in ble_cs_terminate_disable_roles() and to
         * ble_cs_connection_broken(). */
        {
            ble_hs_lock();
            if (cs_state.conn_handle == conn_handle) {
                cs_state.op = 0;
            }
            ble_hs_unlock();
        }
        return rc;
    }

    /* Continues in ble_hs_hci_evt_le_cs_proc_enable_complete() on state
     * 0x00. */
    return 0;
}

int
ble_cs_reflector_setup(const struct ble_cs_reflector_setup_params *params)
{
    int rc;

    if (params == NULL) {
        return BLE_HS_EINVAL;
    }

    ble_hs_lock();
    cs_state.conn_handle = params->conn_handle;
    /* Same reason as ble_cs_initiator_procedure_start(): start the session
     * with no teardown half-armed from a previous one. */
    cs_state.op = 0;
    cs_state.cb = params->cb;
    cs_state.cb_arg = params->cb_arg;
    ble_hs_unlock();

    /* Enable the reflector role now, before the initiator starts the over-air
     * CS setup. Doing this here (rather than reactively in the capability-
     * exchange handler) guarantees the controller has the reflector role
     * enabled when the initiator creates the config and runs security. */
    rc = ble_cs_apply_default_settings(params->conn_handle,
                                       BLE_CS_ROLE_ENABLE_REFLECTOR);
    if (rc != 0) {
        /* Same reasoning as ble_cs_initiator_procedure_start(). */
        ble_cs_state_clear(params->conn_handle);
    }

    return rc;
}

int
ble_cs_procedure_restart(uint16_t conn_handle, uint8_t config_id)
{
    struct ble_cs_proc_enable_cp cmd;

    cmd.conn_handle = conn_handle;
    cmd.config_id = config_id;
    cmd.enable = 0x01;

    return ble_cs_proc_enable(&cmd);
}
#endif
