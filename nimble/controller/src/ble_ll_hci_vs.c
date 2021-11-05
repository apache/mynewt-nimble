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

#include <stdint.h>
#include "syscfg/syscfg.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_hci.h"
#include "controller/ble_hw.h"

#if MYNEWT_VAL(BLE_LL_HCI_VS)

SLIST_HEAD(ble_ll_hci_vs_list, ble_ll_hci_vs_cmd);
static struct ble_ll_hci_vs_list g_ble_ll_hci_vs_list;

static int
ble_ll_hci_vs_rd_static_addr(uint16_t ocf,
                             const uint8_t *cmdbuf, uint8_t cmdlen,
                             uint8_t *rspbuf, uint8_t *rsplen)
{
    struct ble_hci_vs_rd_static_addr_rp *rsp = (void *) rspbuf;
    ble_addr_t addr;

    if (cmdlen != 0) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    if (ble_hw_get_static_addr(&addr) < 0) {
        return BLE_ERR_UNSPECIFIED;
    }

    memcpy(rsp->addr, addr.val, sizeof(rsp->addr));

    *rsplen = sizeof(*rsp);

    return BLE_ERR_SUCCESS;
}

static struct ble_ll_hci_vs_cmd g_ble_ll_hci_vs_cmds[] = {
    BLE_LL_HCI_VS_CMD(BLE_HCI_OCF_VS_RD_STATIC_ADDR,
                      ble_ll_hci_vs_rd_static_addr),
};

static struct ble_ll_hci_vs_cmd *
ble_ll_hci_vs_find_by_ocf(uint16_t ocf)
{
    struct ble_ll_hci_vs_cmd *entry;

    entry = SLIST_FIRST(&g_ble_ll_hci_vs_list);
    while (entry) {
        if (entry->ocf == ocf) {
            return entry;
        }

        entry = SLIST_NEXT(entry, link);
    }

    return NULL;
}

int
ble_ll_hci_vs_cmd_proc(const uint8_t *cmdbuf, uint8_t cmdlen, uint16_t ocf,
                       uint8_t *rspbuf, uint8_t *rsplen)
{
    struct ble_ll_hci_vs_cmd *cmd;
    int rc;

    cmd = ble_ll_hci_vs_find_by_ocf(ocf);
    if (!cmd) {
        rc = BLE_ERR_UNKNOWN_HCI_CMD;
    } else {
        rc = cmd->cb(ocf, cmdbuf, cmdlen, rspbuf, rsplen);
    }

    return rc;
}

void
ble_ll_hci_vs_register(struct ble_ll_hci_vs_cmd *cmds, uint32_t num_cmds)
{
    uint32_t i;

    /* Assume all cmds are registered early on init, so just assert in case of
     * invalid request since it means something is wrong with the code itself.
     */

    for (i = 0; i < num_cmds; i++, cmds++) {
        BLE_LL_ASSERT(cmds->cb != NULL);
        BLE_LL_ASSERT(ble_ll_hci_vs_find_by_ocf(cmds->ocf) == NULL);

        SLIST_INSERT_HEAD(&g_ble_ll_hci_vs_list, cmds, link);
    }
}

void
ble_ll_hci_vs_init(void)
{
    SLIST_INIT(&g_ble_ll_hci_vs_list);

    ble_ll_hci_vs_register(g_ble_ll_hci_vs_cmds,
                           ARRAY_SIZE(g_ble_ll_hci_vs_cmds));
}

#endif
