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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <termios.h>

/* B0 defined in bits/termios.h collides with nrfx/mdk/nrf52.h */
#undef B0

#include "os/mynewt.h"
#include "native_uart_cfg_priv.h"

/* uint64 is used here to accommodate speed_t, whatever that is. */
static const uint64_t uart_baud_table[][2] = {
#ifdef B50
    { 50, B50 },
#endif
#ifdef B75
    { 75, B75 },
#endif
#ifdef B110
    { 110, B110 },
#endif
#ifdef B134
    { 134, B134 },
#endif
#ifdef B150
    { 150, B150 },
#endif
#ifdef B200
    { 200, B200 },
#endif
#ifdef B300
    { 300, B300 },
#endif
#ifdef B600
    { 600, B600 },
#endif
#ifdef B1200
    { 1200, B1200 },
#endif
#ifdef B1800
    { 1800, B1800 },
#endif
#ifdef B2400
    { 2400, B2400 },
#endif
#ifdef B4800
    { 4800, B4800 },
#endif
#ifdef B9600
    { 9600, B9600 },
#endif
#ifdef B19200
    { 19200, B19200 },
#endif
#ifdef B38400
    { 38400, B38400 },
#endif
#ifdef B57600
    { 57600, B57600 },
#endif
#ifdef B115200
    { 115200, B115200 },
#endif
#ifdef B230400
    { 230400, B230400 },
#endif
#ifdef B460800
    { 460800, B460800 },
#endif
#ifdef B500000
    { 500000, B500000 },
#endif
#ifdef B576000
    { 576000, B576000 },
#endif
#ifdef B921600
    { 921600, B921600 },
#endif
#ifdef B1000000
    { 1000000, B1000000 },
#endif
#ifdef B1152000
    { 1152000, B1152000 },
#endif
#ifdef B1500000
    { 1500000, B1500000 },
#endif
#ifdef B2000000
    { 2000000, B2000000 },
#endif
#ifdef B2500000
    { 2500000, B2500000 },
#endif
#ifdef B3000000
    { 3000000, B3000000 },
#endif
#ifdef B3500000
    { 3500000, B3500000 },
#endif
#ifdef B3710000
    { 3710000, B3710000 },
#endif
#ifdef B4000000
    { 4000000, B4000000 },
#endif
};
#define UART_BAUD_TABLE_SZ (sizeof uart_baud_table / sizeof uart_baud_table[0])

/**
 * Returns 0 on failure.
 */
speed_t
uart_baud_to_speed(int_least32_t baud)
{
    int i;

    for (i = 0; i < UART_BAUD_TABLE_SZ; i++) {
        if (uart_baud_table[i][0] == baud) {
            return uart_baud_table[i][1];
        }
    }

    return 0;
}

/**
 * Configures an external device terminal (/dev/cu.<...>).
 */
int
uart_dev_set_attr(int fd, int32_t baudrate, uint8_t databits,
                  uint8_t stopbits, enum hal_uart_parity parity,
                  enum hal_uart_flow_ctl flow_ctl)
{
    struct termios tty;
    speed_t speed;
    int rc;

    assert(fd >= 0);

    memset(&tty, 0, sizeof(tty));
    cfmakeraw(&tty);

    speed = uart_baud_to_speed(baudrate);
    if (speed == 0) {
        fprintf(stderr, "invalid baud rate: %d\n", (int)baudrate);
        assert(0);
    }

    tty.c_cflag |= (speed | CLOCAL | CREAD);

    /* Set flow control. */
    switch (flow_ctl) {
    case HAL_UART_FLOW_CTL_NONE:
        tty.c_cflag &= ~CRTSCTS;
        break;

    case HAL_UART_FLOW_CTL_RTS_CTS:
        tty.c_cflag |= CRTSCTS;
        break;

    default:
        fprintf(stderr, "invalid flow control setting: %d\n", flow_ctl);
        return -1;
    }

    errno = 0;
    rc = cfsetospeed(&tty, speed);
    if (rc != 0) {
        fprintf(stderr, "cfsetospeed failed; %d (%s) baudrate=%d\n",
                errno, strerror(errno), (int)baudrate);
        return -1;
    }

    errno = 0;
    rc = cfsetispeed(&tty, speed);
    if (rc != 0) {
        fprintf(stderr, "cfsetispeed failed; %d (%s) baudrate=%d\n",
                errno, strerror(errno), (int)baudrate);
        return -1;
    }

    switch (databits) {
    case 7:
        tty.c_cflag |= CS7;

        switch (parity) {
        case HAL_UART_PARITY_ODD:
            tty.c_cflag |= PARENB;
            tty.c_cflag |= PARODD;
            tty.c_cflag &= ~CSTOPB;
            tty.c_cflag &= ~CSIZE;
            break;

        case HAL_UART_PARITY_EVEN:
            tty.c_cflag |= PARENB;
            tty.c_cflag &= ~PARODD;
            tty.c_cflag &= ~CSTOPB;
            tty.c_cflag &= ~CSIZE;
            break;

        default:
            return SYS_EINVAL;
        }

    case 8:
        if (parity != HAL_UART_PARITY_NONE) {
            return SYS_EINVAL;
        }
        tty.c_cflag |= CS8;
        tty.c_cflag &= ~PARENB;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CSIZE;
        break;

    default:
        return SYS_EINVAL;
    }

    rc = tcsetattr(fd, TCSANOW, &tty);
    if (rc != 0) {
        fprintf(stderr, "tcsetattr failed; %d (%s)\n", errno, strerror(errno));
        return -1;
    }

    return 0;
}
