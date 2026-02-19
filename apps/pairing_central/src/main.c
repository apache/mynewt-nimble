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

#include "sysinit/sysinit.h"
#include "os/os.h"
#include "console/console.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "log/log.h"
#include "shell/shell.h"
#include "console/console.h"

static uint8_t g_own_addr_type;
static uint16_t conn_handle;

static void scan(void);
static int confirm_passkey(int, char **);
static int deny_passkey(int, char **);

/* Structs required to create custom shell commands. */
/*
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

static void
ble_app_set_addr(void)
{
    ble_addr_t addr;
    int rc;

    /* generate new non-resolvable private address */
    rc = ble_hs_id_gen_rnd(0, &addr);
    assert(rc == 0);

    /* set generated address */
    rc = ble_hs_id_set_rnd(addr.val);
    assert(rc == 0);
}

/*
 * Function to configure security parameters in security manager:
 * - Secure connections is ON
 * - MITM (Man In The Middle) flag is SET
 * - Bonding is OFF
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

    pk.action = 4;
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

/* connection has separate event handler from scan */
static int
conn_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            console_printf("Connection was established\n");
            conn_handle = event->connect.conn_handle;
            /*
             * This function initiates the pairing if the devices are not
             * bonded yet or starts encryption if they are.
             */
            ble_gap_security_initiate(event->connect.conn_handle);
        } else {
            console_printf("Connection failed, error code: %i\n",
                event->connect.status);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        console_printf("Disconnected, reason code: %i\n",
        event->disconnect.reason);
        scan();
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
        ble_gap_terminate(event->connect.conn_handle, 0x13);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        console_printf("Encryption change, status: %s\n",
                       event->enc_change.status == 0 ? "success" : "failed");
        /*
         * If the user does not confirm or deny the passkeys
         * at a given time, encryption change will be timed out.
         * In that case we disconnect.
         */
        if (event->enc_change.status == BLE_HS_ETIMEOUT) {
            ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        break;
    default:
        MODLOG_DFLT(ERROR, "GAP event not handled,"
                    "event code: %u\n", event->type);
        break;
    }
    return 0;
}

static int
scan_event(struct ble_gap_event *event, void *arg)
{
    /* predef_uuid stores information about UUID of device,
       that we connect to */
    const ble_uuid128_t predef_uuid =
        BLE_UUID128_INIT(0x92, 0x98, 0xae, 0x44, 0x47, 0x90, 0xfa, 0x83,
                         0x32, 0x4c, 0xb5, 0xaa, 0x6f, 0xaa, 0x8c, 0x72);
    struct ble_hs_adv_fields parsed_fields;
    int uuid_cmp_result;

    memset(&parsed_fields, 0, sizeof(parsed_fields));

    switch (event->type) {
    /* advertising report has been received during discovery procedure */
    case BLE_GAP_EVENT_DISC:
        ble_hs_adv_parse_fields(&parsed_fields, event->disc.data,
                                event->disc.length_data);
        /* Predefined UUID is compared to recieved one;
           if doesn't fit - end procedure and go back to scanning,
           else - connect. */
        uuid_cmp_result = ble_uuid_cmp(&predef_uuid.u, &parsed_fields.uuids128->u);
        if (!uuid_cmp_result) {
            MODLOG_DFLT(INFO, "Device with fitting UUID found, connecting...\n");
            ble_gap_disc_cancel();
            ble_gap_connect(g_own_addr_type, &(event->disc.addr), 10000,
                            NULL, conn_event, NULL);
        }
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        MODLOG_DFLT(INFO,"Discovery completed, reason: %d\n",
                    event->disc_complete.reason);
        scan();
        break;
    default:
        MODLOG_DFLT(ERROR, "Discovery event not handled\n");
        break;
    }
    return 0;
}

static void
scan(void)
{
    int rc;

    /*
     * set scan parameters:
     *   - scan interval in 0.625ms units
     *   - scan window in 0.625ms units
     *   - filter policy - 0 if whitelisting not used
     *   - limited - should limited discovery be used
     *   - passive - should passive scan be used
     *   - filter duplicates - 1 enables filtering duplicated advertisements
     */
    const struct ble_gap_disc_params scan_params = {
        .itvl = 10000,
        .window = 200,
        .filter_policy = 0,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 1
    };

    /* performs discovery procedure */
    rc = ble_gap_disc(g_own_addr_type, 10000, &scan_params,scan_event, NULL);
    assert(rc == 0);
}

static void
on_sync(void)
{
    int rc;
    /* Generate a non-resolvable private address. */
    ble_app_set_addr();

    /* g_own_addr_type will store type of addres our BSP uses */

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    assert(rc == 0);
    set_security_data();

    /* begin scanning */
    scan();
}

static void
on_reset(int reason)
{
    console_printf("Resetting state; reason=%d\n", reason);
}

int
main(int argc, char **argv)
{
    /* Initialize all packages. */
    sysinit();

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

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
