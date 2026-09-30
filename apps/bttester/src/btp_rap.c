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

/* btp_rap.c - Bluetooth Ranging Profile Tester */

#include "btp/bttester.h"
#include "syscfg/syscfg.h"
#include <string.h>


#if MYNEWT_VAL(BLE_CHANNEL_SOUNDING)

#include "btp/btp_rap.h"

#include "btp/btp.h"
#include "console/console.h"

#include "host/ble_hs.h"
#include "host/ble_cs.h"
#include "host/util/util.h"
#include "math.h"
#if MYNEWT_VAL(BLE_SVC_RAS_CLIENT) || MYNEWT_VAL(BLE_SVC_RAS_SERVER)
#include "services/ras/ble_svc_ras.h"
#endif

int ble_hs_hci_evt_le_cs_subevent_result(uint8_t subevent, const void *data, unsigned int len);

static void
rap_ranging_complete_ev(uint16_t conn_handle, uint8_t status)
{
    struct btp_rap_ranging_complete_ev *ev;
    struct os_mbuf *buf = os_msys_get(0, 0);
    struct ble_gap_conn_desc conn;
    const ble_addr_t *addr;

    SYS_LOG_DBG("");

    if (ble_gap_conn_find(conn_handle, &conn)) {
        return;
    }

    ev = os_mbuf_extend(buf, sizeof(*ev));
    if (!ev) {
        return;
    }

    addr = &conn.peer_ota_addr;
    memcpy(&ev->address, addr, sizeof(ev->address));
    ev->status = status;

    tester_event(BTP_SERVICE_ID_RAP, BTP_RAP_EV_RANGING_COMPLETE,
                 buf->om_data, buf->om_len);
}

static int
btp_rap_event(struct ble_cs_event *event, void *arg, uint16_t conn_handle)
{
    switch (event->type) {
    case BLE_CS_EVENT_CS_PROCEDURE_COMPLETE:
        rap_ranging_complete_ev(conn_handle, event->procedure_complete.status);
        break;
    }

    return 0;
}

static uint8_t
supported_commands(const void *cmd, uint16_t cmd_len,
                   void *rsp, uint16_t *rsp_len)
{
    struct btp_rap_read_supported_commands_rp *rp = rsp;

    *rsp_len = tester_supported_commands(BTP_SERVICE_ID_RAP, rp->data);
    *rsp_len += sizeof(*rp);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
btp_rap_set_test_method(const void *cmd, uint16_t cmd_len,
                        void *rsp, uint16_t *rsp_len)
{
    SYS_LOG_DBG("");
//    struct btp_rap_set_test_method_cmd *cp = cmd;

    return 0;
}

static uint8_t
btp_rap_start_ranging(const void *cmd, uint16_t cmd_len,
                      void *rsp, uint16_t *rsp_len)
{
    int rc;
//    struct ble_cs_procedure_start_params start_cmd;
    const struct btp_rap_start_ranging_cmd *cp = cmd;
    struct ble_cs_setup_params setup_cmd;
    struct ble_gap_conn_desc conn;

    SYS_LOG_DBG("");

    rc = ble_gap_conn_find_by_addr(&cp->address, &conn);
    if (rc) {
        SYS_LOG_DBG("failed, disconnected, %u", rc);
        return BTP_STATUS_FAILED;
    }

    setup_cmd.cb = btp_rap_event;
    setup_cmd.cb_arg = NULL;
    setup_cmd.local_role = cp->local_role;
    ble_cs_setup(&setup_cmd, conn.conn_handle);

#if MYNEWT_VAL(BLE_SVC_RAS_SERVER)
    ble_svc_ras_ranging_data_body_init(conn.conn_handle, 0, 0, 0, 1);
#endif
//    rc = ble_cs_procedure_start(&start_cmd, conn.conn_handle);
//    if (rc) {
//        // send response with error.
//        return rc;
//    }

    return 0;
}

static uint8_t
btp_rap_stop_ranging(const void *cmd, uint16_t cmd_len,
                     void *rsp, uint16_t *rsp_len)
{
    SYS_LOG_DBG("");

    return 0;
}

static uint8_t
btp_rap_set_test_cs_subevent_data(const void *cmd, uint16_t cmd_len,
                                  void *rsp, uint16_t *rsp_len)
{
    int rc;
    struct ble_hci_ev *hci_ev;
    const struct btp_rap_set_test_cs_subevent_data_cmd *cp = cmd;
    struct ble_hci_ev_le_subev_cs_subevent_result *ev;
    struct ble_gap_conn_desc conn;

    SYS_LOG_DBG("");

    if (BLE_HCI_MAX_DATA_LEN - sizeof(ev->subev_code) - sizeof(ev->conn_handle) < cp->data_len) {
        return BTP_STATUS_FAILED;
    }

    rc = ble_gap_conn_find_by_addr(&cp->address, &conn);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    hci_ev = ble_transport_alloc_evt(0);
    if (!hci_ev) {
        return BTP_STATUS_FAILED;
    }

    hci_ev->opcode = BLE_HCI_EVCODE_LE_META;
    hci_ev->length = sizeof(ev->subev_code) + sizeof(ev->conn_handle) + cp->data_len;
    memset(hci_ev->data, 0, BLE_HCI_MAX_DATA_LEN);
    ev = (struct ble_hci_ev_le_subev_cs_subevent_result *)hci_ev->data;

    ev->subev_code = BLE_HCI_LE_SUBEV_CS_SUBEVENT_RESULT;
    ev->conn_handle = htole16(conn.conn_handle);
    memcpy(&ev->config_id, cp->data, cp->data_len);

    ble_hs_hci_evt_le_cs_subevent_result(BLE_HCI_LE_SUBEV_CS_SUBEVENT_RESULT, ev, hci_ev->length);

    return 0;
}

static const struct btp_handler handlers[] = {
    {
        .opcode = BTP_RAP_READ_SUPPORTED_COMMANDS,
        .index = BTP_INDEX_NONE,
        .expect_len = 0,
        .func = supported_commands,
    },
    {
        .opcode = BTP_RAP_SET_TEST_METHOD,
        .index = BTP_INDEX,
        .expect_len = sizeof(struct btp_rap_set_test_method_cmd),
        .func = btp_rap_set_test_method,
    },
    {
        .opcode = BTP_RAP_START_RANGING,
        .index = BTP_INDEX,
        .expect_len = sizeof(struct btp_rap_start_ranging_cmd),
        .func = btp_rap_start_ranging,
    },
    {
        .opcode = BTP_RAP_STOP_RANGING,
        .index = BTP_INDEX,
        .expect_len = sizeof(struct btp_rap_stop_ranging_cmd),
        .func = btp_rap_stop_ranging,
    },
    {
        .opcode = BTP_RAP_SET_TEST_CS_SUBEVENT_DATA,
        .index = BTP_INDEX,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = btp_rap_set_test_cs_subevent_data,
    },
};

uint8_t
tester_init_rap(void)
{
    tester_register_command_handlers(BTP_SERVICE_ID_RAP, handlers,
                                     ARRAY_SIZE(handlers));

    return BTP_STATUS_SUCCESS;
}

uint8_t
tester_unregister_rap(void)
{
    return BTP_STATUS_SUCCESS;
}

#endif /* MYNEWT_VAL(BLE_CHANNEL_SOUNDING) */

