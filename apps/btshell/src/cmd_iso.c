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

#include "host/ble_hs.h"
#include "host/ble_iso.h"

#include "cmd_iso.h"

#include "console/console.h"
#include "shell/shell.h"

#if (MYNEWT_VAL(BLE_ISO))
static struct iso_rx_stats {
    uint16_t conn_handle;
    bool ts_valid;
    uint32_t ts;
    uint16_t seq_num;
    uint64_t total_cnt;
    uint64_t valid_cnt;
    uint64_t error_cnt;
    uint64_t lost_cnt;
} rx_stats_pool[MYNEWT_VAL(BLE_ISO_MAX_BISES)] = {
    [0 ... MYNEWT_VAL(BLE_ISO_MAX_BISES) - 1] = {
        .conn_handle = BLE_HS_CONN_HANDLE_NONE
    }
};

static struct iso_rx_stats *
iso_rx_stats_lookup_conn_handle(uint16_t conn_handle)
{
    for (size_t i = 0; i < ARRAY_SIZE(rx_stats_pool); i++) {
        if (rx_stats_pool[i].conn_handle == conn_handle) {
            return &rx_stats_pool[i];
        }
    }

    return NULL;
}

static struct iso_rx_stats *
iso_rx_stats_get_or_new(uint16_t conn_handle)
{
    struct iso_rx_stats *rx_stats;

    rx_stats = iso_rx_stats_lookup_conn_handle(conn_handle);
    if (rx_stats == NULL) {
        rx_stats = iso_rx_stats_lookup_conn_handle(BLE_HS_CONN_HANDLE_NONE);
        if (rx_stats == NULL) {
            return NULL;
        }
    }

    rx_stats->conn_handle = conn_handle;

    return rx_stats;
}

static void
iso_rx_stats_reset(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(rx_stats_pool); i++) {
        memset(&rx_stats_pool[i], 0, sizeof(rx_stats_pool[i]));
        rx_stats_pool[i].conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
}

static void
iso_rx_stats_update(uint16_t conn_handle, const struct ble_iso_rx_data_info *info, void *arg)
{
    struct iso_rx_stats *stats = arg;

    if (!stats) {
        return;
    }

    stats->ts_valid = info->ts_valid;
    if (stats->ts_valid) {
        stats->ts = info->ts;
    }

    stats->seq_num = info->seq_num;

    if (info->status == BLE_ISO_DATA_STATUS_VALID) {
        stats->valid_cnt++;
    } else if (info->status == BLE_ISO_DATA_STATUS_ERROR) {
        stats->error_cnt++;
    } else if (info->status == BLE_ISO_DATA_STATUS_LOST) {
        stats->lost_cnt++;
    }

    stats->total_cnt++;

    if ((stats->total_cnt % 100) == 0) {
        console_printf("conn_handle=0x%04x, seq_num=%d, num_rx=%" PRIu64 ", "
                       "(valid=%" PRIu64 ", error=%" PRIu64 ", lost=%" PRIu64 ") ",
                       stats->conn_handle, stats->seq_num,
                       stats->total_cnt, stats->valid_cnt,
                       stats->error_cnt, stats->lost_cnt);

        if (stats->ts_valid) {
            console_printf("ts=10%" PRIu32, stats->ts);
        }

        console_printf("\n");
    }
}

static void
print_iso_big_desc(const struct ble_iso_big_desc *desc)
{
    console_printf(" big_handle=0x%02x, big_sync_delay=%" PRIu32 ","
                   " transport_latency=%" PRIu32 ", nse=%u, bn=%u, pto=%u,"
                   " irc=%u, max_pdu=%u, iso_interval=%u num_bis=%u",
                   desc->big_handle, desc->big_sync_delay,
                   desc->transport_latency_big, desc->nse, desc->bn, desc->pto,
                   desc->irc, desc->max_pdu, desc->iso_interval, desc->num_bis);

    if (desc->num_bis > 0) {
        console_printf(" conn_handles=");
    }

    for (uint8_t i = 0; i < desc->num_bis; i++) {
        console_printf("0x%04x,", desc->conn_handle[i]);
    }
}

static int
ble_iso_event_handler(struct ble_iso_event *event, void *arg)
{
    switch (event->type) {
    case BLE_ISO_EVENT_BIG_CREATE_COMPLETE:
        console_printf("BIG Create Completed status: %u",
                       event->big_created.status);

        if (event->big_created.status == 0) {
            print_iso_big_desc(&event->big_created.desc);
            console_printf(" phy=0x%02x", event->big_created.phy);
        }

        console_printf("\n");
        break;

    case BLE_ISO_EVENT_BIG_SYNC_ESTABLISHED:
        console_printf("BIG Sync Established status: %u",
                       event->big_sync_established.status);

        if (event->big_sync_established.status == 0) {
            print_iso_big_desc(&event->big_sync_established.desc);
        }

        console_printf("\n");
        break;

    case BLE_ISO_EVENT_BIG_SYNC_TERMINATED:
        console_printf("BIG Sync Terminated handle=0x%02x reason: %u\n",
                       event->big_terminated.big_handle,
                       event->big_terminated.reason);
        iso_rx_stats_reset();
        break;

    case BLE_ISO_EVENT_ISO_RX:
        iso_rx_stats_update(event->iso_rx.conn_handle, event->iso_rx.info, arg);
        os_mbuf_free_chain(event->iso_rx.om);
        break;

    default:
        break;
    }

    return 0;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_iso_big_create_params[] = {
    {"adv_handle", "PA advertising handle, usage: =<UINT8>"},
    {"bis_cnt", "BIS count, usage: =<UINT8>"},
    {"sdu_interval", "SDU interval, usage: =<UINT32>"},
    {"max_sdu", "Maximum SDU size, usage: =<UINT16>"},
    {"max_latency", "Maximum transport latency, usage: =<UINT16>"},
    {"rtn", "Retransmission number, usage: =<UINT8>"},
    {"phy", "PHY, usage: =<UINT8>"},
    {"packing", "Packing, usage: =<UINT8>, default: 1"},
    {"framing", "Framing, usage: =<UINT8>, default: 0"},
    {"broadcast_code", "Broadcast Code, usage: =[string], default: NULL"},

    { NULL, NULL}
};

const struct shell_cmd_help cmd_iso_big_create_help = {
    .summary = "Create BIG",
    .usage = NULL,
    .params = cmd_iso_big_create_params,
};
#endif /* SHELL_CMD_HELP */

int
cmd_iso_big_create(int argc, char **argv)
{
    struct ble_iso_create_big_params params = { 0 };
    struct ble_iso_big_params big_params = { 0 };
    uint8_t big_handle;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    params.adv_handle = parse_arg_uint8("adv_handle", &rc);
    if (rc != 0) {
        console_printf("invalid 'adv_handle' parameter\n");
        return rc;
    }

    params.cb = ble_iso_event_handler;

    params.bis_cnt = parse_arg_uint8("bis_cnt", &rc);
    if (rc != 0) {
        console_printf("invalid 'bis_cnt' parameter\n");
        return rc;
    }

    big_params.sdu_interval = parse_arg_uint32_bounds("sdu_interval",
                                                      0x0000FF, 0x0FFFFF,
                                                      &rc);
    if (rc != 0) {
        console_printf("invalid 'sdu_interval' parameter\n");
        return rc;
    }

    big_params.max_sdu = parse_arg_uint16_bounds("max_sdu", 0x0001, 0x0FFF,
                                                 &rc);
    if (rc != 0) {
        console_printf("invalid 'max_sdu' parameter\n");
        return rc;
    }

    big_params.max_transport_latency = parse_arg_uint16_bounds("max_latency",
                                                               0x0005, 0x0FA0,
                                                               &rc);
    if (rc != 0) {
        console_printf("invalid 'max_latency' parameter\n");
        return rc;
    }

    big_params.rtn = parse_arg_uint8_bounds("rtn", 0x00, 0x1E, &rc);
    if (rc != 0) {
        console_printf("invalid 'rtn' parameter\n");
        return rc;
    }

    big_params.phy = parse_arg_uint8_bounds("phy", 0, 2, &rc);
    if (rc != 0) {
        console_printf("invalid 'phy' parameter\n");
        return rc;
    }

    big_params.packing = parse_arg_uint8_bounds_dflt("packing", 0, 1, 1, &rc);
    if (rc != 0) {
        console_printf("invalid 'packing' parameter\n");
        return rc;
    }

    big_params.framing = parse_arg_uint8_bounds_dflt("framing", 0, 1, 0, &rc);
    if (rc != 0) {
        console_printf("invalid 'framing' parameter\n");
        return rc;
    }

    big_params.broadcast_code = parse_arg_extract("broadcast_code");
    big_params.encryption = big_params.broadcast_code ? 1 : 0;

    rc = ble_iso_create_big(&params, &big_params, &big_handle);
    if (rc != 0) {
        console_printf("BIG create failed (%d)\n", rc);
        return rc;
    }

    console_printf("New big_handle %u created\n", big_handle);

    return 0;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_iso_big_terminate_params[] = {
    {"big_handle", "BIG handle, usage: =<UINT8>"},

    { NULL, NULL}
};

const struct shell_cmd_help cmd_iso_big_terminate_help = {
    .summary = "Terminate BIG",
    .usage = NULL,
    .params = cmd_iso_big_terminate_params,
};
#endif /* SHELL_CMD_HELP */

int
cmd_iso_big_terminate(int argc, char **argv)
{
    uint8_t big_handle;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    big_handle = parse_arg_uint8("big_handle", &rc);
    if (rc != 0) {
        console_printf("invalid 'big_handle' parameter\n");
        return rc;
    }

    rc = ble_iso_terminate_big(big_handle);
    if (rc != 0) {
        console_printf("BIG terminate failed (%d)\n", rc);
        return rc;
    }

    return 0;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_iso_big_sync_create_params[] = {
    {"sync_handle", "PA sync handle, usage: =<UINT16>"},
    {"broadcast_code", "Broadcast Code, usage: =[string], default: NULL"},
    {"mse", "Maximum Subevents to receive data, usage: =<UINT8>"},
    {"sync_timeout", "BIG sync timeout, usage: =<UINT8>"},
    {"idxs", "BIS indexes, usage: =XX,YY,..."},

    { NULL, NULL}
};

const struct shell_cmd_help cmd_iso_big_sync_create_help = {
    .summary = "Synchronize to BIG",
    .usage = NULL,
    .params = cmd_iso_big_sync_create_params,
};
#endif /* SHELL_CMD_HELP */

int
cmd_iso_big_sync_create(int argc, char **argv)
{
    struct ble_iso_bis_params bis_params[MYNEWT_VAL(BLE_ISO_MAX_BISES)];
    struct ble_iso_big_sync_create_params params = { 0 };
    uint8_t bis_idxs[MYNEWT_VAL(BLE_ISO_MAX_BISES)];
    uint8_t big_handle;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    params.sync_handle = parse_arg_uint16("sync_handle", &rc);
    if (rc != 0) {
        console_printf("invalid 'sync_handle' parameter\n");
        return rc;
    }

    params.broadcast_code = parse_arg_extract("broadcast_code");

    params.mse = parse_arg_uint8_dflt("mse", 0, &rc);
    if (rc != 0) {
        console_printf("invalid 'mse' parameter\n");
        return rc;
    }

    params.sync_timeout = parse_arg_uint16("sync_timeout", &rc);
    if (rc != 0) {
        console_printf("invalid 'sync_timeout' parameter\n");
        return rc;
    }

    rc = parse_arg_byte_stream_custom("idxs", ",", ARRAY_SIZE(bis_idxs),
                                      bis_idxs, 0,
                                      (unsigned int *)&params.bis_cnt);
    if (rc != 0) {
        console_printf("invalid 'idxs' parameter\n");
        return rc;
    }

    for (uint8_t i = 0; i < params.bis_cnt; i++) {
        bis_params[i].bis_index = bis_idxs[i];
    }

    params.bis_params = bis_params;
    params.cb = ble_iso_event_handler;

    rc = ble_iso_big_sync_create(&params, &big_handle);
    if (rc != 0) {
        console_printf("BIG Sync create failed (%d)\n", rc);
        return rc;
    }

    console_printf("New big_handle %u created\n", big_handle);

    return 0;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_iso_big_sync_terminate_params[] = {
    {"big_handle", "BIG handle, usage: =<UINT8>"},

    { NULL, NULL}
};

const struct shell_cmd_help cmd_iso_big_sync_terminate_help = {
    .summary = "Terminate BIG sync",
    .usage = NULL,
    .params = cmd_iso_big_sync_terminate_params,
};
#endif /* SHELL_CMD_HELP */

int
cmd_iso_big_sync_terminate(int argc, char **argv)
{
    uint8_t big_handle;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    big_handle = parse_arg_uint8("big_handle", &rc);
    if (rc != 0) {
        console_printf("invalid 'big_handle' parameter\n");
        return rc;
    }

    rc = ble_iso_big_sync_terminate(big_handle);
    if (rc != 0) {
        console_printf("BIG Sync terminate failed (%d)\n", rc);
        return rc;
    }

    return 0;
}

static const struct parse_arg_kv_pair cmd_iso_data_dir[] = {
    { "tx",       BLE_ISO_DATA_DIR_TX },
    { "rx",       BLE_ISO_DATA_DIR_RX },

    { NULL }
};

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_iso_data_path_setup_params[] = {
    {"conn_handle", "Connection handle, usage: =<UINT16>"},
    {"dir", "Data path direction, usage: =[tx|rx]"},

    { NULL, NULL}
};

const struct shell_cmd_help cmd_iso_data_path_setup_help = {
    .summary = "Setup ISO Data Path",
    .usage = NULL,
    .params = cmd_iso_data_path_setup_params,
};
#endif /* SHELL_CMD_HELP */

int
cmd_iso_data_path_setup(int argc, char **argv)
{
    struct ble_iso_data_path_setup_params params = { 0 };
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    params.conn_handle = parse_arg_uint16("conn_handle", &rc);
    if (rc != 0) {
        console_printf("invalid 'conn_handle' parameter\n");
        return rc;
    }

    params.data_path_dir = parse_arg_kv("dir", cmd_iso_data_dir, &rc);
    if (rc != 0) {
        console_printf("invalid 'dir' parameter\n");
        return rc;
    }

    /* For now, the Data Path ID is set to HCI by default */
    params.cb = ble_iso_event_handler;
    params.cb_arg = iso_rx_stats_get_or_new(params.conn_handle);

    rc = ble_iso_data_path_setup(&params);
    if (rc != 0) {
        console_printf("ISO Data Path setup failed (%d)\n", rc);
        return rc;
    }

    return 0;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_iso_data_path_remove_params[] = {
    {"conn_handle", "Connection handle, usage: =<UINT16>"},
    {"dir", "Data path direction, usage: =[tx|rx]"},

    { NULL, NULL}
};

const struct shell_cmd_help cmd_iso_data_path_remove_help = {
    .summary = "Remove ISO Data Path",
    .usage = NULL,
    .params = cmd_iso_data_path_remove_params,
};
#endif /* SHELL_CMD_HELP */

int
cmd_iso_data_path_remove(int argc, char **argv)
{
    struct ble_iso_data_path_remove_params params = { 0 };
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    params.conn_handle = parse_arg_uint16("conn_handle", &rc);
    if (rc != 0) {
        console_printf("invalid 'conn_handle' parameter\n");
        return rc;
    }

    params.data_path_dir = parse_arg_kv("dir", cmd_iso_data_dir, &rc);
    if (rc != 0) {
        console_printf("invalid 'dir' parameter\n");
        return rc;
    }

    rc = ble_iso_data_path_remove(&params);
    if (rc != 0) {
        console_printf("ISO Data Path remove failed (%d)\n", rc);
        return rc;
    }

    return 0;
}
#endif /* BLE_ISO */
