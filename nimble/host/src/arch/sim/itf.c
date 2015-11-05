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

#include <os/os.h>

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h> 
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "host/attr.h"
#include "host/host_task.h"
#include "host/host_hci.h"
#include "ble_hs_conn.h"
#include "itf.h"

#define BLE_SIM_BASE_PORT       10000

static void
set_nonblock(int fd)
{
    int flags;
    int rc;

    flags = fcntl(fd, F_GETFL, 0);
    rc = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    assert(rc >= 0);
}


int
ble_sim_listen(uint16_t con_handle)
{
    struct ble_hs_conn *conn;
    struct sockaddr_in sin;
    struct hostent *ent;
    int fd;
    int rc;

    conn = ble_hs_conn_alloc();
    if (conn == NULL) {
        rc = ENOMEM;
        goto err;
    }

    /* resolve host addr first, then create a socket and bind() to 
     * that address.
     */
    ent = gethostbyname("localhost");
    if (ent == NULL) {
        rc = errno;
        goto err;
    }

    memset(&sin, 0, sizeof(sin));
    memcpy(&sin.sin_addr, ent->h_addr_list[0], ent->h_length);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(BLE_SIM_BASE_PORT + con_handle);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        rc = errno;
        goto err;
    }

    set_nonblock(fd);

    rc = bind(fd, (struct sockaddr *)&sin, sizeof sin);
    if (rc != 0) {
        close(fd);
        rc = errno;
        goto err;
    }

    memset(conn, 0, sizeof *conn);
    conn->bhc_handle = con_handle;
    conn->bhc_fd = fd;

    return 0;

err:
    ble_hs_conn_free(conn);
    return rc;
}

static int
ble_sim_connect(uint16_t con_handle, struct ble_hs_conn **out_conn)
{
    struct ble_hs_conn *conn;
    struct sockaddr_in sin;
    struct hostent *ent;
    int fd;
    int rc;

    conn = ble_hs_conn_alloc();
    if (conn == NULL) {
        rc = ENOMEM;
        goto err;
    }

    /* resolve host addr first, then create a socket and bind() to 
     * that address.
     */
    ent = gethostbyname("localhost");
    if (ent == NULL) {
        rc = errno;
        goto err;
    }

    memset(&sin, 0, sizeof(sin));
    memcpy(&sin.sin_addr, ent->h_addr_list[0], ent->h_length);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(BLE_SIM_BASE_PORT + con_handle);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        rc = errno;
        goto err;
    }

    set_nonblock(fd);

    rc = connect(fd, (struct sockaddr *)&sin, sizeof sin);
    if (rc != 0 && errno != EINPROGRESS) {
        rc = errno;
        goto err;
    }

    memset(conn, 0, sizeof *conn);
    conn->bhc_handle = con_handle;
    conn->bhc_fd = fd;

    *out_conn = conn;

    return 0;

err:
    ble_hs_conn_free(conn);
    *out_conn = NULL;
    return rc;
}

static int
ble_sim_ensure_connection(uint16_t con_handle, struct ble_hs_conn **conn)
{
    int rc;

    *conn = ble_hs_conn_find(con_handle);
    if (*conn == NULL) {
        rc = ble_sim_connect(con_handle, conn);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

static int
ble_sim_send(uint16_t con_handle, const void *data, uint16_t len)
{
    struct ble_hs_conn *conn;
    int rc;
    int i;

    rc = ble_sim_ensure_connection(con_handle, &conn);
    if (rc != 0) {
        return rc;
    }

    printf("sending %d bytes: ", len);
    for (i = 0; i < len; i++) {
        printf("%02x ", ((const uint8_t *)data)[i]);
    }
    printf("\n");

    while (len > 0) {
        rc = send(conn->bhc_fd, data, len, 0);
        if (rc >= 0) {
            data += rc;
            len -= rc;
        } else {
            return errno;
        }
    }

    return 0;
}

int
ble_host_sim_send_data_connectionless(uint16_t con_handle, uint16_t cid,
                                      uint8_t *data, uint16_t len)
{
    static uint8_t buf[1024];
    int off;
    int rc;

    off = 0;

    htole16(buf + off, con_handle | (0 << 12) | (0 << 14));
    off += 2;

    htole16(buf + off, len + 4);
    off += 2;

    htole16(buf + off, len);
    off += 2;

    htole16(buf + off, cid);
    off += 2;

    memcpy(buf + off, data, len);
    off += len;

    rc = ble_sim_send(con_handle, buf, off);
    return rc;
}

int 
ble_host_sim_poll(void)
{
    static uint8_t buf[1024];
    struct ble_hs_conn *conn;
    fd_set r_fd_set;
    fd_set w_fd_set;
    uint16_t pkt_size;
    int nevents;
    int max_fd;
    int fd;
    int rc;
    int i;

    FD_ZERO(&r_fd_set);
    FD_ZERO(&w_fd_set);

    max_fd = 0;
    for (conn = ble_hs_conn_first();
         conn != NULL;
         conn = SLIST_NEXT(conn, bhc_next)) {

        fd = conn->bhc_fd;
        FD_SET(fd, &r_fd_set);
        FD_SET(fd, &w_fd_set);
        if (fd > max_fd) {
            max_fd = fd;
        }
    }

    do {
        nevents = select(max_fd + 1, &r_fd_set, &w_fd_set, NULL, NULL);
    } while (nevents < 0 && errno == EINTR);

    if (nevents > 0) {
    for (conn = ble_hs_conn_first();
         conn != NULL;
         conn = SLIST_NEXT(conn, bhc_next)) {

            if (FD_ISSET(conn->bhc_fd, &r_fd_set)) {
                while (1) {
                    rc = recv(conn->bhc_fd, buf, sizeof buf, 0);
                    if (rc <= 0) {
                        break;
                    }
                    pkt_size = rc;

                    printf("received HCI data packet (%d bytes): ", pkt_size);
                    for (i = 0; i < pkt_size; i++) {
                        printf("%02x ", buf[i]);
                    }
                    printf("\n");

                    rc = host_hci_data_rx(buf, pkt_size);
                }
            }
        }
    }

    return 0;
}
