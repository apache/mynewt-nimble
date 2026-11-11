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

/*
 * Minimal UART driver which sends all data to stdout of the simulated
 * device process. Rx is not supported.
 */

#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include "os/mynewt.h"
#include "hal/hal_uart.h"

struct bsim_uart {
    hal_uart_tx_char tx_func;
    hal_uart_tx_done tx_done;
    void *arg;
    uint8_t open;
};

static struct bsim_uart g_uart;

static void
uart_write(const uint8_t *buf, int len)
{
    ssize_t rc;

    while (len > 0) {
        rc = write(STDOUT_FILENO, buf, len);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            /* Nothing we can do, drop output */
            return;
        }

        buf += rc;
        len -= rc;
    }
}

int
hal_uart_init_cbs(int port, hal_uart_tx_char tx_func, hal_uart_tx_done tx_done,
                  hal_uart_rx_char rx_func, void *arg)
{
    if (port != 0 || g_uart.open) {
        return -1;
    }

    g_uart.tx_func = tx_func;
    g_uart.tx_done = tx_done;
    g_uart.arg = arg;

    return 0;
}

void
hal_uart_start_tx(int port)
{
    uint8_t buf[64];
    int len;
    int ch;

    if (port != 0 || !g_uart.open) {
        return;
    }

    do {
        len = 0;
        while (len < sizeof(buf)) {
            ch = g_uart.tx_func(g_uart.arg);
            if (ch < 0) {
                break;
            }
            buf[len++] = ch;
        }

        uart_write(buf, len);
    } while (len == sizeof(buf));

    if (g_uart.tx_done) {
        g_uart.tx_done(g_uart.arg);
    }
}

void
hal_uart_start_rx(int port)
{
}

void
hal_uart_blocking_tx(int port, uint8_t data)
{
    if (port != 0 || !g_uart.open) {
        return;
    }

    uart_write(&data, 1);
}

int
hal_uart_config(int port, int32_t baudrate, uint8_t databits, uint8_t stopbits,
                enum hal_uart_parity parity, enum hal_uart_flow_ctl flow_ctl)
{
    if (port != 0 || g_uart.open) {
        return -1;
    }

    g_uart.open = 1;

    return 0;
}

int
hal_uart_close(int port)
{
    if (port != 0 || !g_uart.open) {
        return -1;
    }

    g_uart.open = 0;

    return 0;
}

int
hal_uart_init(int port, void *arg)
{
    return port == 0 ? 0 : -1;
}
