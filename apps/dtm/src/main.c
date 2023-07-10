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

#include "os/mynewt.h"
#include <console/console.h>
#include <parse.h>
#include <shell/shell.h>
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "host/ble_hs.h"
#include "host/ble_dtm.h"
#include <img_mgmt/img_mgmt.h>
#include <bootutil/image.h>

static const struct kv_pair phy_opts[] = {
    { "1M",          0x01 },
    { "2M",          0x02 },
    { "coded",       0x03 },
    { NULL }
};

static const struct kv_pair modulation_index_opts[] = {
    { "standard",    0x00 },
    { "stable",      0x01 },
    { NULL }
};

static int
cmd_rx_test(int argc, char **argv)
{
    struct ble_dtm_rx_params params;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    params.channel = parse_arg_uint8("channel", &rc);
    if ((rc != 0) || (params.channel > 39)) {
        console_printf("invalid channel\n");
        return rc;
    }

    params.phy = parse_arg_kv_dflt("phy", phy_opts, 0x01, &rc);
    if (rc != 0) {
        console_printf("invalid 'phy' parameter\n");
        return rc;
    }

    params.modulation_index = parse_arg_kv_dflt("modulation_index",
                                                modulation_index_opts, 0x00, &rc);
    if (rc != 0) {
        console_printf("invalid 'modulation_index' parameter\n");
        return rc;
    }

    rc = ble_dtm_rx_start(&params);
    if (rc) {
        console_printf("failed to start RX test\n");
        return rc;
    }

    console_printf("RX test started\n");
    return 0;
}

static int
cmd_tx_test(int argc, char **argv)
{
    struct ble_dtm_tx_params params;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    params.channel = parse_arg_uint8("channel", &rc);
    if ((rc != 0) || (params.channel > 39)) {
        console_printf("invalid channel\n");
        return rc;
    }

    params.phy = parse_arg_kv_dflt("phy", phy_opts, 0x01, &rc);
    if (rc != 0) {
        console_printf("invalid 'phy' parameter\n");
        return rc;
    }

    params.payload = parse_arg_uint8("payload", &rc);
    if ((rc != 0) || ((params.payload > 7))) {
        console_printf("invalid 'payload' parameter\n");
        return rc;
    }

    params.test_data_len = parse_arg_uint8_dflt("data_length", 0, &rc);
    if (rc != 0) {
        console_printf("invalid 'data_length' parameter\n");
        return rc;
    }

    rc = ble_dtm_tx_start(&params);
    if (rc) {
        console_printf("failed to start TX test\n");
        return rc;
    }

    console_printf("TX test started\n");
    return 0;
}

static int
cmd_stop_test(int argc, char **argv)
{
    uint16_t num_packets;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    rc = ble_dtm_stop(&num_packets);
    if (rc) {
        console_printf("failed to stop test\n");
        return rc;
    }

    console_printf("Test stopped (%u packets)\n", num_packets);
    return 0;
}

static int
cmd_tx_power(int argc, char **argv)
{
    struct ble_hci_vs_set_tx_pwr_cp cmd;
    struct ble_hci_vs_set_tx_pwr_rp rsp;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    cmd.tx_power = parse_arg_long_bounds_dflt("power",
                                             -127, 127, 127, &rc);
    if (rc != 0) {
        console_printf("invalid 'power' parameter\n");
        return rc;
    }

    rc = ble_hs_hci_send_vs_cmd(BLE_HCI_OCF_VS_SET_TX_PWR, &cmd, sizeof(cmd),
                               &rsp, sizeof(rsp));
    if (rc) {
        console_printf("failed to set TX power\n");
        return rc;
    }

    console_printf("TX power set to %d dBm\n", rsp.tx_power);
    return 0;
}

static int
cmd_set_antenna(int argc, char **argv)
{
    struct ble_hci_vs_set_antenna_cp cmd;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    cmd.antenna = parse_arg_uint8_dflt("antenna", 0, &rc);
    if (rc != 0 || ((cmd.antenna > 2))) {
        console_printf("invalid 'antenna' parameter\n");
        return rc;
    }

    rc = ble_hs_hci_send_vs_cmd(BLE_HCI_OCF_VS_SET_ANTENNA, &cmd, sizeof(cmd),
                                NULL, 0);
    if (rc) {
        console_printf("failed to set antenna\n");
        return rc;
    }

    console_printf("Antenna set to %u\n", cmd.antenna);
    return 0;
}

#define BLE_HCI_OCF_VS_TEST_CARRIER (0x0020)

static bool tx_carrier_running = false;

static int
cmd_tx_carrier(int argc, char **argv)
{
    uint8_t cmd[2];
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    if (tx_carrier_running) {
        console_printf("TX carrier already started\n");
        return 0;
    }

    cmd[0] = 1;
    cmd[1] = parse_arg_uint8("channel", &rc);
    if ((rc != 0) || (cmd[1] > 39)) {
        console_printf("invalid channel\n");
        return rc;
    }

    rc = ble_hs_hci_send_vs_cmd(BLE_HCI_OCF_VS_TEST_CARRIER, &cmd, sizeof(cmd),
                                NULL, 0);
    if (rc) {
        console_printf("failed to start TX carrier\n");
        return rc;
    }

    console_printf("TX carrier started\n");
    tx_carrier_running = true;
    return 0;
}

static int
cmd_stop_carrier(int argc, char **argv)
{
    uint8_t cmd[2];
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    if (!tx_carrier_running) {
        console_printf("TX carrier not started\n");
        return 0;
    }
    cmd[0] = 0;
    cmd[1] = 0;

    rc = ble_hs_hci_send_vs_cmd(BLE_HCI_OCF_VS_TEST_CARRIER, &cmd, sizeof(cmd),
                                NULL, 0);
    if (rc) {
        console_printf("failed to stop TX carrier\n");
        return rc;
    }

    console_printf("TX carrier stopped\n");
    tx_carrier_running = false;
    return 0;
}

static const struct shell_param cmd_rx_test_params[] = {
    {"channel", "RX channel, usage: =[0-39]"},
    {"phy", "usage: =[1M|2M], default: 1M"},
    {"modulation_index", "usage: =[standard|stable], default=standard"},
    {NULL}
};

static const struct shell_cmd_help cmd_rx_test_help = {
    .summary = "start DTM RX test",
    .usage = NULL,
    .params = cmd_rx_test_params,
};

static const struct shell_param cmd_tx_test_params[] = {
    {"channel", "RX channel, usage: =[0-39]"},
    {"phy", "usage: =[1M|2M], default: 1M"},
    {"data_length", "usage: =[0-255], default: 0"},
    {"payload", "usage: =[0-7]"},
    {NULL}
};

static const struct shell_cmd_help cmd_tx_test_help = {
    .summary = "start DTM TX test",
    .usage = NULL,
    .params = cmd_tx_test_params,
};

static const struct shell_cmd_help cmd_stop_test_help = {
    .summary = "stop DTM test",
    .usage = NULL,
    .params = NULL,
};

static const struct shell_param cmd_tx_power_params[] = {
    {"power", "usage: =[-127-127], default: 127"},
    {NULL}
};

static const struct shell_cmd_help cmd_tx_power_help = {
    .summary = "set TX power",
    .usage = NULL,
    .params = cmd_tx_power_params,
};

static const struct shell_param cmd_set_antenna_params[] = {
    {"antenna", "usage: =[0,1,2], default: 0"},
    {NULL}
};

static const struct shell_cmd_help cmd_set_antenna_help = {
    .summary = "set active antenna ",
    .usage = NULL,
    .params = cmd_set_antenna_params,
};

static const struct shell_param cmd_tx_carrier_params[] = {
    {"channel", "TX channel, usage: =[0-39]"},
    {NULL}
};

static const struct shell_cmd_help cmd_tx_carrier_help = {
    .summary = "TX unmodulated carrier",
    .usage = NULL,
    .params = cmd_tx_carrier_params,
};

static const struct shell_cmd_help cmd_stop_carrier_help = {
    .summary = "stop TX unmodulated carrier",
    .usage = NULL,
    .params = NULL,
};

static const struct shell_cmd dtm_commands[] = {
    {
        .sc_cmd = "rx-test",
        .sc_cmd_func = cmd_rx_test,
        .help = &cmd_rx_test_help,
    },
    {
        .sc_cmd = "tx-test",
        .sc_cmd_func = cmd_tx_test,
        .help = &cmd_tx_test_help,
    },
    {
        .sc_cmd = "stop-test",
        .sc_cmd_func = cmd_stop_test,
        .help = &cmd_stop_test_help,
    },
    {
        .sc_cmd = "tx-power",
        .sc_cmd_func = cmd_tx_power,
        .help = &cmd_tx_power_help,
    },
    {
        .sc_cmd = "set-antenna",
        .sc_cmd_func = cmd_set_antenna,
        .help = &cmd_set_antenna_help,
    },
    {
        .sc_cmd = "tx-carrier",
        .sc_cmd_func = cmd_tx_carrier,
        .help = &cmd_tx_carrier_help,
    },
    {
        .sc_cmd = "stop-carrier",
        .sc_cmd_func = cmd_stop_carrier,
        .help = &cmd_stop_carrier_help,
    },
    { }
};

static void
on_sync(void)
{
    console_printf("Host and controller synced\n");
}

static void
on_reset(int reason)
{
    console_printf("Error: Resetting state; reason=%d\n", reason);
}

int
main(void)
{
    struct image_version the_version;
    char prompt[50];

    sysinit();

    img_mgmt_read_info(0, &the_version, NULL, NULL);

    snprintf(prompt, sizeof(prompt), "dtm_%u.%u.%u",
             the_version.iv_major, the_version.iv_minor, the_version.iv_revision);

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    shell_register(prompt, dtm_commands);
    shell_register_default_module(prompt);

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
