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

#include "os/os.h"
#include "sysinit/sysinit.h"
#include "log/log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "shell/shell.h"
#include "console/console.h"

static uint8_t g_own_addr_type;
static uint16_t conn_handle;
static const char *device_name = "Mynewt";

static void advertise(void);
static int confirm_passkey(int argc, char **argv);
static int deny_passkey(int argc, char **argv);

/* Structs required to create custom shell commands.
 *
 * shell_cmd_help struct is responsible for configuring what
 * is going to be printed in the console
 * after entering command "help" or pressing TAB
 */
static const struct shell_cmd_help confirm_passkey_help = {
    .summary = "Confirm passkey",
    .usage = NULL,
    .params = NULL,
};

static const struct shell_cmd_help deny_passkey_help = {
    .summary = "Deny passkey",
    .usage = NULL,
    .params = NULL,
};

/* shell_cmd struct allows to configure custom command */
static const struct shell_cmd shell_commands[] = {
    {
        .sc_cmd = "confirm",
        .sc_cmd_func = confirm_passkey,
        .help = &confirm_passkey_help,
    },
    {
        .sc_cmd = "deny",
        .sc_cmd_func = deny_passkey,
        .help = &deny_passkey_help,
    },
    { 0 },
};

/*
 * Function to configure security parameters in security manager:
 * - Secure Connections is ON
 * - MITM (Man In The Middle) flag is SET
 * - Bonding is ON
 * - IO Capabilities are set to display and yes/no input available
 */
static void
set_security_data(void)
{
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
}

/*
 * Function called after confirming by the user
 * that the passkeys generated on both devices are the same
 */
static int
confirm_passkey(int argc, char **argv)
{
    int rc;
    struct ble_sm_io pk;

    /*
     * Both devices have display and yes/no io capabilities.
     * Based on that the passkey action is set to numeric comparison.
     */
    pk.action = BLE_SM_IOACT_NUMCMP;
    pk.numcmp_accept = 1;

    /* Function called to handle pairing */
    rc = ble_sm_inject_io(conn_handle, &pk);
    if (rc != 0) {
        console_printf("error providing passkey; rc=%d\n", rc);
        return rc;
    }
    return 0;
}

/* Function called when the user denies that the codes match */
static int
deny_passkey(int argc, char **argv)
{
    int rc;
    struct ble_sm_io pk;

    /*
     * Both devices have display and yes/no io capabilities.
     * Based on that the pk action is numeric comparison.
     */
    pk.action = BLE_SM_IOACT_NUMCMP;
    pk.numcmp_accept = 0;

    /* Function called to handle pairing */
    rc = ble_sm_inject_io(conn_handle, &pk);
    if (rc != 0) {
        console_printf("error providing passkey; rc=%d\n", rc);
        return rc;
    }
    return 0;
}

/* Various gap events callback */
static int
gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_ADV_COMPLETE:
        console_printf("Advertising completed, termination code: %d\n",
                       event->adv_complete.reason);
        advertise();
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            console_printf("Connection was established\n");
            conn_handle = event->connect.conn_handle;
        } else {
            console_printf("Connection failed, error code: %i\n",
                           event->connect.status);
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        console_printf("disconnect; reason=%d\n",
                       event->disconnect.reason);

        /* reset conn_handle */
        conn_handle = BLE_HS_CONN_HANDLE_NONE;

        /* Connection terminated; resume advertising */
        advertise();
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        console_printf("passkey action event; action=%d\n",
                       event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            console_printf("Confirm or deny key: %lu\n",
                           (unsigned long)event->passkey.params.numcmp);
        }
        break;
    case BLE_GAP_EVENT_PARING_COMPLETE:
        console_printf("Pairing attempt finished, pairing %s\n",
                       event->pairing_complete.status == 0 ? "successful" : "failed");
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        console_printf("Encryption change, status: %s\n",
                       event->enc_change.status == 0 ? "success" : "failed");
        break;
    default:
        MODLOG_DFLT(ERROR, "GAP event not handled,"
                    "event code: %u\n", event->type);
        break;
    }
    return 0;
}

static void
advertise(void)
{
    int rc;

    /* set adv parameters */
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    /* advertising payload is split into advertising data and advertising
       response, because all data cannot fit into single packet; name of device
       is sent as response to scan request */
    struct ble_hs_adv_fields rsp_fields;

    /* fill all fields and parameters with zeros */
    memset(&adv_params, 0, sizeof(adv_params));
    memset(&fields, 0, sizeof(fields));
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = BLE_UUID128(BLE_UUID128_DECLARE(
            0x92, 0x98, 0xae, 0x44, 0x47, 0x90, 0xfa, 0x83,
            0x32, 0x4c, 0xb5, 0xaa, 0x6f, 0xaa, 0x8c, 0x72));
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 0;;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    rsp_fields.name = (uint8_t *)device_name;
    rsp_fields.name_len = strlen(device_name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    assert(rc == 0);

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    assert(rc == 0);

    MODLOG_DFLT(INFO,"Starting advertising...\n");

    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event, NULL);
    assert(rc == 0);
}

static void
on_sync(void)
{
    int rc;

    /* g_own_addr_type will store type of addres our BSP uses */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    assert(rc == 0);

    /* configure security params */
    set_security_data();
    /* begin advertising */
    advertise();
}

static void
on_reset(int reason)
{
    MODLOG_DFLT(INFO, "Resetting state; reason=%d\n", reason);
}

int
main(int argc, char **argv)
{
    int rc;

    /* Initialize all packages. */
    sysinit();

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    rc = ble_svc_gap_device_name_set(device_name);
    assert(rc == 0);

    /*
     * To enable custom shell commands, first we register them, and then
     * we set module with these custom commands as default shell module
     */
    shell_register("app_shell", shell_commands);
    shell_register_default_module("app_shell");

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}