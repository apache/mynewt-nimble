/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef H_BLE_HS_ATT_
#define H_BLE_HS_ATT_

#include "host/uuid.h"

#define BLE_HOST_EVENT_NEW_ATTR_CONN (OS_EVENT_T_PERUSER)

struct ble_att_chan {
    int c_fd;
    int c_state; 
}; 

struct host_attr;

/**
 * Called from host_attr_walk().  Called on each entry in the 
 * host_attr_list.
 *
 * @param Contains the current host_attr being iterated through
 * @param The user supplied argument to host_attr_walk()
 *
 * @return 0 on continue, 1 on stop
 */
typedef int (*host_attr_walk_func_t)(struct host_attr *, void *arg);

/**
 * Handles a host attribute request.
 *
 * @param The host attribute being requested 
 * @param The request data associated with that host attribute
 */
typedef int (*host_attr_handle_func_t)(struct host_attr *, uint8_t *data);

#define HA_FLAG_PERM_READ            (1 << 0)
#define HA_FLAG_PERM_WRITE           (1 << 1) 
#define HA_FLAG_PERM_RW              (1 << 2)
#define HA_FLAG_ENC_REQ              (1 << 3)
#define HA_FLAG_AUTHENTICATION_REQ   (1 << 4)
#define HA_FLAG_AUTHORIZATION_REQ    (1 << 5)

struct host_attr {
    ble_uuid_t ha_uuid;
    uint8_t ha_flags;
    uint8_t ha_pad1;
    uint16_t ha_handle_id;
    STAILQ_ENTRY(host_attr) ha_next;
};

#define HA_OPCODE_METHOD_START (0)
#define HA_OPCODE_METHOD_END (5)
#define HA_OPCODE_COMMAND_FLAG (1 << 6) 
#define HA_OPCODE_AUTH_SIG_FLAG (1 << 7) 


#define HA_METH_ERROR_RSP        (0x01)
#define HA_METH_EXCHANGE_MTU_REQ (0x02)
#define HA_METH_EXCHANGE_MTU_RSP (0x03)

struct ble_l2cap_chan *ble_hs_att_create_chan(void);

#endif
