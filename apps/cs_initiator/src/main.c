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

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <float.h>
#include "os/mynewt.h"
#include "bsp/bsp.h"

/* BLE */
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "host/ble_cs.h"
#include "host/ble_peer.h"
#include "host/util/util.h"

/* Mandatory services. */
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ras/ble_svc_ras.h"

/* Application-specified header. */
#include "cs_initiator.h"
#include <math.h>
#include "console/console.h"

static struct os_callout cs_start_timer;
static int cs_initiator_gap_event(struct ble_gap_event *event, void *arg);
static uint16_t last_conn_handle;
static uint8_t cs_started;

static void cs_initiator_start_cs(uint16_t conn_handle);

#define CHARS_PER_TONE_MAX (64)
static char buf[2000];

static int16_t
sign_extend_12(uint16_t x)
{
    return (int16_t)(x << 4) >> 4;
}

static int
cs_print(struct ble_cs_subevent_info *local_subevent_info,
         struct ble_cs_subevent_info *remote_subevent_info)
{
    int pos = 0;
    int max = sizeof(buf);
    uint8_t number_of_tones;
    uint32_t tone_pct;
    int16_t I12i, Q12i, I12r, Q12r;
    uint16_t i;
    uint8_t channel;

    if (local_subevent_info->mode2_results_count != remote_subevent_info->mode2_results_count) {
        LOG(INFO, "Number of steps, local: %d, remote: %d\n",
            local_subevent_info->mode2_results_count,
            remote_subevent_info->mode2_results_count);
        number_of_tones = min(local_subevent_info->mode2_results_count,
                              remote_subevent_info->mode2_results_count);
    } else {
        number_of_tones = local_subevent_info->mode2_results_count;
    }

    if (number_of_tones == 0) {
        return 1;
    }

    pos += snprintf(buf, max, "%u ", number_of_tones);

    for (i = 0; i < number_of_tones; ++i) {
        if (pos >= max - CHARS_PER_TONE_MAX) {
            break;
        }

        channel = local_subevent_info->mode2_results[i].step_channel;

        tone_pct = local_subevent_info->mode2_results[i].tone_pct[0];
        I12i = sign_extend_12(tone_pct & 0x0FFF);
        Q12i = sign_extend_12((tone_pct >> 12) & 0x0FFF);

        tone_pct = remote_subevent_info->mode2_results[i].tone_pct[0];
        I12r = sign_extend_12(tone_pct & 0x0FFF);
        Q12r = sign_extend_12((tone_pct >> 12) & 0x0FFF);

        pos += snprintf(buf + pos, max - pos, "%u %d %d %d %d ",
                        channel, I12i, Q12i, I12r, Q12r);
    }

    if (pos < max - 2) {
        buf[pos++] = '\n';
        buf[pos] = '\0';
    } else {
        buf[max - 2] = '\n';
        buf[max - 1] = '\0';
    }

    printf("%s", buf);

    return 0;
}

static void
cs_initiator_start_cs(uint16_t conn_handle)
{
    struct ble_cs_procedure_start_params cmd;
    int rc;

    LOG(INFO, "Starting new CS procedure, conn_handle %d\n", conn_handle);

    last_conn_handle = conn_handle;

    rc = ble_cs_procedure_start(&cmd, conn_handle);
    if (rc) {
        LOG(INFO, "Failed to start new CS procedure, err %d\n", rc);
    }
}

static void
cs_start_timer_reset(void)
{
    int rc;

    rc = os_callout_reset(&cs_start_timer, os_time_ms_to_ticks32(500));
    assert(rc == 0);
}

static void
cs_start_timer_exp(struct os_event *ev)
{
    cs_initiator_start_cs(last_conn_handle);
}

static int
cs_initiator_procedure_complete(struct ble_cs_event *event)
{
    LOG(INFO, "CS procedure completed, status=%d\n", event->procedure_complete.status);

    cs_start_timer_reset();

    return 0;
}

static int
cs_initiator_subevent_data_received(struct ble_cs_event *event)
{
    LOG(INFO, "CS subevent data received, status=%d\n", event->subevent_data.status);

    if (event->subevent_data.status == 0) {
        cs_print(event->subevent_data.local_subevent_info,
                 event->subevent_data.remote_subevent_info);
    }

    return 0;
}

static int
cs_initiator_cs_event(struct ble_cs_event *event, void *arg, uint16_t conn_handle)
{
    switch (event->type) {
    case BLE_CS_EVENT_CS_PROCEDURE_COMPLETE:
        cs_initiator_procedure_complete(event);
        break;
    case BLE_CS_EVENT_CS_SUBEVENT_DATA:
        cs_initiator_subevent_data_received(event);
        break;
    }

    return 0;
}

/**
 * Initiates the GAP general discovery procedure.
 */
static void
cs_initiator_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_ext_disc_params params = {0};
    int rc;

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        LOG(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    params.itvl = BLE_GAP_SCAN_ITVL_MS(40);
    params.passive = 1;
    params.window = BLE_GAP_SCAN_FAST_WINDOW;

    rc = ble_gap_ext_disc(own_addr_type, 30000, 0, 0, 0, 0, &params, NULL,
                          cs_initiator_gap_event, NULL);
    if (rc != 0) {
        LOG(ERROR, "Error initiating GAP discovery procedure; rc=%d\n", rc);
    }
}

/**
 * Indicates whether we should tre to connect to the sender of the specified
 * advertisement.  The function returns a positive result if the device
 * advertises connectability and support for the Alert Notification service.
 */
static int
cs_initiator_should_connect(const ble_addr_t *addr, const struct ble_hs_adv_fields *fields)
{
    int i;

    if (addr == NULL) {
        return 0;
    }

    if (fields->name != NULL && !memcmp((const char *)fields->name, "Pixel 9", 7)) {
        return 1;
    }

    for (i = 0; i < fields->num_uuids16; i++) {
        if (ble_uuid_u16(&fields->uuids16[i].u) == BLE_SVC_RAS_SVC_RANGING_SERVICE_UUID) {
            return 1;
        }
    }

    return 0;
}

/**
 * Connects to the sender of the specified advertisement of it looks
 * interesting.  A device is "interesting" if it advertises connectability and
 * support for the Alert Notification service.
 */
static void
cs_initiator_connect_if_interesting(const struct ble_gap_disc_desc *disc,
                                    const struct ble_gap_ext_disc_desc *ext_disc)
{
    int rc;
    const ble_addr_t *addr;
    struct ble_hs_adv_fields fields;
    uint8_t own_addr_type;
    uint8_t length_data;
    const uint8_t *data;
    struct ble_gap_conn_params conn_params = {
        .scan_itvl = 0x0010,
        .scan_window = 0x0010,
        .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN,
        .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
        .latency = BLE_GAP_INITIAL_CONN_LATENCY,
        .supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT,
        .min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN,
        .max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN,
    };

    if (disc) {
        if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
            disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
            return;
        }
        data = disc->data;
        length_data = disc->length_data;
        addr = &disc->addr;
    } else if (ext_disc) {
        if (ext_disc->props & BLE_HCI_ADV_LEGACY_MASK &&
            (ext_disc->legacy_event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
             ext_disc->legacy_event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)) {
            return;
        } else if (!(ext_disc->props & BLE_HCI_ADV_CONN_MASK)) {
            return;
        }
        data = ext_disc->data;
        length_data = ext_disc->length_data;
        addr = &ext_disc->addr;
    } else {
        return;
    }

    rc = ble_hs_adv_parse_fields(&fields, data, length_data);
    if (rc != 0) {
        return;
    }

    /* An advertisment report was received during GAP discovery. */
//    print_adv_fields(&fields);

    /* Don't do anything if we don't care about this advertiser. */
    if (!cs_initiator_should_connect(addr, &fields)) {
        return;
    }

    /* Scanning must be stopped before a connection can be initiated. */
    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        LOG(DEBUG, "Failed to cancel scan; rc=%d\n", rc);
        return;
    }

    /* Figure out address to use for connect (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        LOG(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Try to connect the the advertiser.  Allow 30 seconds (30000 ms) for
     * timeout.
     */
    if (disc) {
        rc = ble_gap_connect(own_addr_type, addr, 0, &conn_params,
                             cs_initiator_gap_event, NULL);
    } else {
        rc = ble_gap_ext_connect(own_addr_type, addr, 0, BLE_GAP_LE_PHY_1M_MASK,
                                 &conn_params, NULL, NULL, cs_initiator_gap_event, NULL);
    }

    if (rc != 0) {
        LOG(ERROR, "Error: Failed to connect to device; addr_type=%d "
            "addr=%s; rc=%d\n", disc->addr.type, addr_str(disc->addr.val), rc);
        return;
    }
}

static void
cs_initiator_on_disc_complete(const struct ble_peer *peer, int status, void *arg)
{

    if (status != 0) {
        /* Service discovery failed.  Terminate the connection. */
        LOG(ERROR, "Error: Service discovery failed; status=%d "
            "conn_handle=%d\n", status, peer->conn_handle);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    /* Service discovery has completed successfully.  Now we have a complete
     * list of services, characteristics, and descriptors that the peer
     * supports.
     */
    LOG(INFO, "Service discovery complete; status=%d "
        "conn_handle=%d\n", status, peer->conn_handle);

    cs_initiator_start_cs(peer->conn_handle);
}

static int
cs_mtu_updated(uint16_t conn_handle, const struct ble_gatt_error *error,
               uint16_t mtu, void *arg)
{
    int rc;
    struct ble_cs_setup_params cmd;

    if (cs_started) {
        return 0;
    }

    cs_started = 1;
    cmd.cb = cs_initiator_cs_event;
    cmd.cb_arg = NULL;
    cmd.local_role = BLE_HS_CS_ROLE_INITIATOR;
    ble_cs_setup(&cmd, conn_handle);

    /* Perform service discovery. */
    rc = ble_peer_disc_all(conn_handle, cs_initiator_on_disc_complete, NULL);
    if (rc != 0) {
        LOG(ERROR, "Failed to discover services; rc=%d\n", rc);
        return 0;
    }

    return 0;
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that is
 * established.  cs_initiator uses the same callback for all connections.
 *
 * @param event                 The event being signalled.
 * @param arg                   Application-specified argument; unused by
 *                                  cs_initiator.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
cs_initiator_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_sm_io pk;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_EXT_DISC:
        cs_initiator_connect_if_interesting(NULL, &event->ext_disc);
        return 0;
    case BLE_GAP_EVENT_DISC:
        cs_initiator_connect_if_interesting(&event->disc, NULL);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        if (event->connect.status == 0) {
            /* Connection successfully established. */
            LOG(INFO, "Connection established ");

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);
            LOG(INFO, "\n");

            /* Remember peer. */
            rc = ble_peer_add(event->connect.conn_handle);
            if (rc != 0) {
                LOG(ERROR, "Failed to add peer; rc=%d\n", rc);
                return 0;
            }

            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc) {
                LOG(INFO, "Failed to pair");
            }
        } else {
            /* Connection attempt failed; resume scanning. */
            LOG(ERROR, "Error: Connection failed; status=%d\n",
                event->connect.status);
            cs_initiator_scan();
        }

        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated. */
        LOG(INFO, "disconnect; reason=%d ", event->disconnect.reason);
        print_conn_desc(&event->disconnect.conn);
        LOG(INFO, "\n");

        /* Forget about peer. */
        ble_peer_delete(event->disconnect.conn.conn_handle);

        /* Resume scanning. */
        cs_initiator_scan();

        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        LOG(INFO, "discovery complete; reason=%d\n",
            event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_PAIRING_COMPLETE:
        LOG(INFO, "received pairing complete: "
            "conn_handle=%d status=%d\n",
            event->pairing_complete.conn_handle,
            event->pairing_complete.status);

        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        LOG(INFO, "encryption change event; status=%d ",
            event->enc_change.status);

        if (event->enc_change.status != 0) {
            return 0;
        }

        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);

        rc = ble_gattc_exchange_mtu(event->enc_change.conn_handle, cs_mtu_updated, NULL);
        assert(rc == 0);

        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        /* Peer sent us a notification or indication. */
        LOG(DEBUG, "received %s; conn_handle=%d attr_handle=%d "
            "attr_len=%d\n",
            event->notify_rx.indication ?
            "indication" :
            "notification",
            event->notify_rx.conn_handle,
            event->notify_rx.attr_handle,
            OS_MBUF_PKTLEN(event->notify_rx.om));

        /* Attribute data is contained in event->notify_rx.attr_data. */
        return 0;

    case BLE_GAP_EVENT_MTU:
        LOG(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
            event->mtu.conn_handle,
            event->mtu.channel_id,
            event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

//        /* Delete the old bond. */
//        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
//        assert(rc == 0);
//        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;
#if MYNEWT_VAL(BLE_SM_SC)
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        LOG(INFO, "passkey action event; action=%d", event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            LOG(INFO, " numcmp=%lu", (unsigned long) event->passkey.params.numcmp);

            pk.action = BLE_SM_IOACT_NUMCMP;
            pk.numcmp_accept = 1;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pk);
            assert(rc == 0);
        }
        return 0;
#endif
    default:
        return 0;
    }
}

static void
cs_initiator_on_reset(int reason)
{
    LOG(ERROR, "Resetting state; reason=%d\n", reason);
}

static void
cs_initiator_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Begin scanning for a peripheral to connect to. */
    cs_initiator_scan();
}

/**
 * main
 *
 * All application logic and NimBLE host work is performed in default task.
 *
 * @return int NOTE: this function should never return!
 */
int
mynewt_main(int argc, char **argv)
{
    int rc;

    /* Initialize OS */
    sysinit();

    rc = modlog_register(MODLOG_MODULE_APP, log_console_get(),
                         LOG_LEVEL_DEBUG, NULL);
    assert(rc == 0);

    LOG(DEBUG, "Started CS Initiator\n");

    /* Configure the host. */
    ble_hs_cfg.reset_cb = cs_initiator_on_reset;
    ble_hs_cfg.sync_cb = cs_initiator_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
#if MYNEWT_VAL(BLE_SM_SC)
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
#endif

    os_callout_init(&cs_start_timer, os_eventq_dflt_get(),
                    cs_start_timer_exp, NULL);

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("nimble-cs_initiator");
    assert(rc == 0);

    /* os start should never return. If it does, this should be an error */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
