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

/* All Channel Sounding APIs are experimental and subject to change at any
 * time */

#ifndef H_BLE_CS_
#define H_BLE_CS_
#include "syscfg/syscfg.h"

#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)

#include "host/ble_hs.h"
#include "nimble/hci_common.h"

/** Length of a CS Channel_Map, in octets. */
#define BLE_CS_CHANNEL_MAP_LEN                    (10)

/** CS event: Procedure and its teardown finished, no more events follow */
#define BLE_CS_EVENT_PROCEDURE_COMPLETE           (0)

/** CS event: Procedure enable (or disable) completed in the controller */
#define BLE_CS_EVENT_PROCEDURE_ENABLE_COMPLETE    (1)

/** CS event: First portion of a CS subevent's step results */
#define BLE_CS_EVENT_SUBEVENT_RESULT              (2)

/** CS event: Continuation of a CS subevent's step results */
#define BLE_CS_EVENT_SUBEVENT_RESULT_CONTINUE     (3)

struct ble_cs_event {
    uint8_t type;
    union {
        struct {
            uint16_t conn_handle;
            int status;
        } procedure_complete;
        struct {
            uint16_t conn_handle;
            uint8_t status;
            uint8_t config_id;
            /* Signed dBm: the controller is free to select a level below 0,
             * and the low range is the normal one for CS.  The HCI event
             * struct carries the octet unsigned, so the sign is restored once
             * here instead of at each consumer. */
            int8_t selected_tx_power;
            /* 0x01 when the procedure was enabled, 0x00 when it was
             * disabled (application terminate, or the controller reaching
             * max_procedure_count). */
            uint8_t state;
        } procedure_enable_complete;
        struct {
            uint16_t conn_handle;
            uint8_t config_id;
            uint16_t start_acl_conn_event_counter;
            uint16_t procedure_counter;
            uint16_t frequency_compensation;
            uint8_t reference_power_level;
            uint8_t procedure_done_status;
            uint8_t subevent_done_status;
            uint8_t abort_reason;
            uint8_t num_antenna_paths;
            uint8_t num_steps_reported;
            const struct cs_steps_data *steps;
            /* Bytes of step data behind the pointer above, so a consumer can
             * bound its own walk instead of trusting each step's data_len. */
            uint16_t steps_len;
        } subev_result;
        struct {
            uint16_t conn_handle;
            uint8_t config_id;
            uint8_t procedure_done_status;
            uint8_t subevent_done_status;
            uint8_t abort_reason;
            uint8_t num_antenna_paths;
            uint8_t num_steps_reported;
            const struct cs_steps_data *steps;
            /* See subev_result.steps_len. */
            uint16_t steps_len;
        } subev_result_continue;
    };
};

typedef int ble_cs_event_fn(struct ble_cs_event *event, void *arg);

struct ble_cs_initiator_procedure_start_params {
    uint16_t conn_handle;
    ble_cs_event_fn *cb;
    void *cb_arg;
};

struct ble_cs_reflector_setup_params {
    /** Link to enable the reflector role on.  Every field here is required;
     *  the setup sends an HCI command for this handle and matches later CS
     *  events against it, so a zero-initialised struct targets connection
     *  handle 0 rather than defaulting to the current link. */
    uint16_t conn_handle;
    ble_cs_event_fn *cb;
    void *cb_arg;
};

/** Local controller CS capabilities, host byte order. */
struct ble_cs_local_supp_cap {
    uint8_t num_config_supported;
    uint16_t max_consecutive_procedures_supported;
    uint8_t num_antennas_supported;
    uint8_t max_antenna_paths_supported;
    uint8_t roles_supported;
    uint8_t optional_modes_supported;
    uint8_t rtt_capability;
    uint8_t rtt_aa_only_n;
    uint8_t rtt_sounding_n;
    uint8_t rtt_random_payload_n;
    uint16_t optional_nadm_sounding_capability;
    uint16_t optional_nadm_random_capability;
    uint8_t optional_cs_sync_phys_supported;
    uint16_t optional_subfeatures_supported;
    uint16_t optional_t_ip1_times_supported;
    uint16_t optional_t_ip2_times_supported;
    uint16_t optional_t_fcs_times_supported;
    uint16_t optional_t_pm_times_supported;
    uint8_t t_sw_time_supported;
    uint8_t tx_snr_capability;
};

/**
 * Replace the Channel_Map the host puts in the CS configuration it creates.
 *
 * One bit per CS channel, channel 0 in the LSB of the first octet.  The
 * channels the specification does not allow for Channel Sounding have to stay
 * cleared, and the map has to leave enough channels enabled for the
 * configuration to be usable; the controller rejects the configuration
 * otherwise.  Takes effect on the next configuration the host creates, so set
 * it before starting a procedure rather than during one.
 *
 * @param channel_map   BLE_CS_CHANNEL_MAP_LEN octets, copied by this call.
 * @return              0 on success; BLE_HS_EINVAL if channel_map is NULL.
 */
int ble_cs_set_channel_map(const uint8_t *channel_map);

/**
 * Start a Channel Sounding procedure as initiator.
 *
 * Only the first HCI command of the setup is issued here; the remaining steps
 * are chained off the events they complete with.  The outcome is reported
 * through the supplied callback as BLE_CS_EVENT_PROCEDURE_ENABLE_COMPLETE,
 * after which step data arrives as BLE_CS_EVENT_SUBEVENT_RESULT and
 * BLE_CS_EVENT_SUBEVENT_RESULT_CONTINUE events.
 *
 * The link must already be encrypted - Channel Sounding has no unencrypted
 * mode.  Only one CS procedure is tracked at a time; starting a second one
 * replaces the first one's callback and connection handle.
 *
 * @param params        Connection handle and event callback.  The callback and
 *                      its argument are retained until the procedure is
 *                      terminated or the connection drops.
 * @return              0 on success; BLE_HS_EENCRYPT if the link is not
 *                      encrypted; NimBLE error code on other failures.
 */
int ble_cs_initiator_procedure_start(
    const struct ble_cs_initiator_procedure_start_params *params);

/**
 * Tear down the initiator-side setup created by
 * ble_cs_initiator_procedure_start(): disable the procedure, remove its
 * configuration and disable the CS roles.
 *
 * Each of those commands only reports its outcome in a later LE meta event, so
 * the teardown is chained off them rather than issued in one go - the
 * controller would reject a config removal while the procedure is still
 * enabled, and a role change while a configuration still exists.  A 0 return
 * therefore only means the teardown was started; its outcome is reported
 * through the callback registered at start as
 * BLE_CS_EVENT_PROCEDURE_COMPLETE.
 *
 * @param conn_handle   Connection handle.
 * @return              0 if the teardown was started; BLE_HS_ENOENT if no CS
 *                      session is tracked for this connection; other NimBLE
 *                      error code if the first command could not be issued,
 *                      in which case no completion event follows.
 */
int ble_cs_initiator_procedure_terminate(uint16_t conn_handle);

/**
 * Enable the Channel Sounding reflector role on a connection.
 *
 * Call this as soon as the connection is established and before pairing: the
 * controller rejects the initiator's capability and config requests unless the
 * reflector role is already enabled.  The reflector issues no setup commands
 * of its own - the initiator drives the procedure, and results are delivered
 * through the supplied callback.  Only one CS procedure is tracked at a time.
 *
 * This host drives the initiator side on links where the local device is the
 * central, so enable the reflector role only on links where the local device
 * is the peripheral.
 *
 * @param params        conn_handle is mandatory: it selects the link the
 *                      default settings are applied to and is the handle every
 *                      CS event is matched against, so a zero-initialised
 *                      params targets connection handle 0.  The callback and
 *                      its argument are retained until the connection drops.
 * @return              0 on success; NimBLE error code on failure.  Check it -
 *                      on failure the reflector role is not enabled and the
 *                      callback has already been released.
 */
int ble_cs_reflector_setup(const struct ble_cs_reflector_setup_params *params);

/**
 * Read the local controller's Channel Sounding capabilities
 * (HCI LE CS Read Local Supported Capabilities, opcode 0x2089).
 *
 * The peer uses num_antennas_supported and max_antenna_paths_supported to
 * derive the tone antenna config index it sends in its own
 * LE CS Set Procedure Parameters, so a mismatch here breaks the procedure
 * with no locally visible error.
 *
 * @param out           Filled in on success.
 * @return              0 on success; NimBLE error code on failure.
 */
int ble_cs_read_local_supp_cap(struct ble_cs_local_supp_cap *out);

/**
 * Re-enable CS procedure on an already-configured connection.
 * Use after the procedure has been disabled - by the application, or by the
 * controller reaching max_procedure_count when BLE_CS_MAX_PROCEDURE_COUNT is
 * built as non-zero - to start the next measurement without repeating the
 * full setup phase (capabilities exchange, default settings, config create,
 * security).  The default build uses a free-running procedure stream, so this
 * is not needed there.
 *
 * @param conn_handle   Connection handle.
 * @param config_id     CS config ID (typically 0x00).
 * @return              0 on success; NimBLE error code on failure.
 */
int ble_cs_procedure_restart(uint16_t conn_handle, uint8_t config_id);

#endif /* MYNEWT_VAL(BLE_CHANNEL_SOUNDING) */
#endif /* H_BLE_CS_ */
