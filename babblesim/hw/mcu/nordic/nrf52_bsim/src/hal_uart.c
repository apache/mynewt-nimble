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
#include "hal/hal_uart.h"
#include "bsp/bsp.h"

#ifdef MN_LINUX
#include <pty.h>
#endif
#ifdef MN_OSX
#include <util.h>
#endif
#ifdef MN_FreeBSD
#include <libutil.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <errno.h>

#include "mcu/mcu_sim.h"
#include "native_uart_cfg_priv.h"
#include "syscfg/syscfg.h"

#define UART_CNT                2

#if MYNEWT_VAL(CONSOLE_UART_TX_BUF_SIZE)
#define UART_MAX_BYTES_PER_POLL	MYNEWT_VAL(CONSOLE_UART_TX_BUF_SIZE) - 2
#else
#define UART_MAX_BYTES_PER_POLL	64
#endif
#define UART_POLLER_STACK_SZ	OS_STACK_ALIGN(1024)

struct uart {
    int u_open;
    int u_fd;
    int u_tx_run;
    int u_rx_char;
    hal_uart_rx_char u_rx_func;
    hal_uart_tx_char u_tx_func;
    hal_uart_tx_done u_tx_done;
    void *u_func_arg;
};

const char *native_uart_dev_strs[UART_CNT];

/*
 * XXXX should try to use O_ASYNC/SIGIO for byte arrival notification,
 * so we wouldn't need an OS for pseudo ttys.
 */
char *native_uart_log_file = NULL;
static int uart_log_fd = -1;

static struct uart uarts[UART_CNT];
static int uart_poller_running;
static struct os_task uart_poller_task;
static os_stack_t uart_poller_stack[UART_POLLER_STACK_SZ];

static void
uart_open_log(void)
{
    if (native_uart_log_file && uart_log_fd < 0) {
        uart_log_fd = open(native_uart_log_file, O_WRONLY | O_CREAT | O_TRUNC,
                0666);
        assert(uart_log_fd >= 0);
    }
}

static void
uart_log_data(struct uart *u, int istx, uint8_t data)
{
    static struct {
        struct uart *uart;
        int istx;
        uint32_t time;
        int chars_in_line;
    } state = {
            .uart = NULL,
            .istx = 0
    };
    uint32_t now;
    char tmpbuf[32];
    int len;

    if (uart_log_fd < 0) {
        return;
    }
    now = os_time_get();
    if (state.uart) {
        if (u != state.uart || now != state.time || istx != state.istx) {
            /*
             * End current printout.
             */
            if (write(uart_log_fd, "\n", 1) != 1) {
                assert(0);
            }
            state.uart = NULL;
        } else {
            if (state.chars_in_line == 8) {
                if (write(uart_log_fd, "\n\t", 2) != 2) {
                    assert(0);
                }
                state.chars_in_line = 0;
            }
            len = snprintf(tmpbuf, sizeof(tmpbuf), "%c (%02x) ",
                    isalnum(data) ? data : '?', data);
            if (write(uart_log_fd, tmpbuf, len) != len) {
                assert(0);
            }
            state.chars_in_line++;
        }
    }
    if (u && state.uart == NULL) {
        len = snprintf(tmpbuf, sizeof(tmpbuf), "%u:uart%d %s\n\t%c (%02x) ",
                now, u - uarts, istx ? "tx" : "rx", isalnum(data) ? data : '?', data);
        if (write(uart_log_fd, tmpbuf, len) != len) {
            assert(0);
        }
        state.chars_in_line = 1;
        state.uart = u;
        state.istx = istx;
        state.time = now;
    }
}

static int
uart_transmit_char(struct uart *uart)
{
    int sr;
    int rc;
    char ch;

    OS_ENTER_CRITICAL(sr);
    rc = uart->u_tx_func(uart->u_func_arg);
    if (rc < 0) {
        /*
         * No more data to send.
         */
        uart->u_tx_run = 0;
        if (uart->u_tx_done) {
            uart->u_tx_done(uart->u_func_arg);
        }
        OS_EXIT_CRITICAL(sr);
        return 0;
    }
    ch = rc;
    uart_log_data(uart, 1, ch);
    OS_EXIT_CRITICAL(sr);
    rc = write(uart->u_fd, &ch, 1);
    if (rc <= 0) {
        /* XXX EOF/error, what now? */
        return -1;
    }
    return 0;
}

static void
uart_poller(void *arg)
{
    int i;
    int rc;
    int bytes;
    int sr;
    int didwork;
    unsigned char ch;
    struct uart *uart;

    while (1) {
        for (i = 0; i < UART_CNT; i++) {
            if (!uarts[i].u_open) {
                continue;
            }
            uart = &uarts[i];

            for (bytes = 0; bytes < UART_MAX_BYTES_PER_POLL; bytes++) {
                didwork = 0;
                if (uart->u_tx_run) {
                    uart_transmit_char(uart);
                    didwork = 1;
                }
                if (uart->u_rx_char < 0) {
                    rc = read(uart->u_fd, &ch, 1);
                    if (rc == 0) {
                        /* XXX EOF, what now? */
                        assert(0);
                    } else if (rc > 0) {
                        uart->u_rx_char = ch;
                    }
                }
                if (uart->u_rx_char >= 0) {
                    OS_ENTER_CRITICAL(sr);
                    uart_log_data(uart, 0, uart->u_rx_char);
                    rc = uart->u_rx_func(uart->u_func_arg, uart->u_rx_char);
                    /* Delivered */
                    if (rc >= 0) {
                        uart->u_rx_char = -1;
                        didwork = 1;
                    }
                    OS_EXIT_CRITICAL(sr);
                }
                if (!didwork) {
                    break;
                }
            }
        }
        uart_log_data(NULL, 0, 0);
        os_time_delay(OS_TICKS_PER_SEC / 100);
    }
}

static void
set_nonblock(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        const char msg[] = "fcntl(F_GETFL) fail";
        write(1, msg, sizeof(msg));
        return;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        const char msg[] = "fcntl(F_SETFL) fail";
        write(1, msg, sizeof(msg));
        return;
    }
}

static int
uart_pty_set_attr(int fd)
{
    struct termios tios;

    if (tcgetattr(fd, &tios)) {
        const char msg[] = "tcgetattr() failed";
        write(1, msg, sizeof(msg));
        return -1;
    }

    tios.c_cflag &= ~(CSIZE | CSTOPB | PARENB);
    tios.c_cflag |= CS8 | CREAD;
    tios.c_iflag = IGNPAR;
    tios.c_oflag = 0;
    tios.c_lflag = 0;
    if (tcsetattr(fd, TCSAFLUSH, &tios) < 0) {
        const char msg[] = "tcsetattr() failed";
        write(1, msg, sizeof(msg));
        return -1;
    }
    return 0;
}

static int
uart_pty(int port)
{
    int fd;
    int loop_slave;
    char pty_name[32];
    char msg[64];

    if (openpty(&fd, &loop_slave, pty_name, NULL, NULL) < 0) {
        const char msg[] = "openpty() failed";
        write(1, msg, sizeof(msg));
        return -1;
    }

    if (uart_pty_set_attr(loop_slave)) {
        goto err;
    }

    snprintf(msg, sizeof(msg), "uart%d at %s\n", port, pty_name);
    write(1, msg, strlen(msg));
    return fd;
    err:
    close(fd);
    close(loop_slave);
    return -1;
}

/**
 * Opens an external device terminal (/dev/cu.<...>).
 */
static int
uart_open_dev(int port, int32_t baudrate, uint8_t databits,
              uint8_t stopbits, enum hal_uart_parity parity,
              enum hal_uart_flow_ctl flow_ctl)
{
    const char *filename;
    int fd;
    int rc;

    filename = native_uart_dev_strs[port];
    assert(filename != NULL);

    fd = open(filename, O_RDWR);
    if (fd < 0) {
        return -1;
    }

    rc = uart_dev_set_attr(fd, baudrate, databits,
            stopbits, parity, flow_ctl);
    if (rc != 0) {
        close(fd);
        return rc;
    }

    dprintf(1, "uart%d at %s\n", port, filename);

    return fd;
}

void
hal_uart_start_tx(int port)
{
    int sr;

    if (port >= UART_CNT || uarts[port].u_open == 0) {
        return;
    }
    OS_ENTER_CRITICAL(sr);
    uarts[port].u_tx_run = 1;
    if (!os_started()) {
        /*
         * XXX this is a hack.
         */
        uart_transmit_char(&uarts[port]);
    }
    OS_EXIT_CRITICAL(sr);
}

void
hal_uart_start_rx(int port)
{
    /* nothing to do here */
}

void
hal_uart_blocking_tx(int port, uint8_t data)
{
    if (port >= UART_CNT || uarts[port].u_open == 0) {
        return;
    }

    /* XXX: Count statistics and add error checking here. */
    (void) write(uarts[port].u_fd, &data, sizeof(data));
}

int
hal_uart_init_cbs(int port, hal_uart_tx_char tx_func, hal_uart_tx_done tx_done,
                  hal_uart_rx_char rx_func, void *arg)
{
    struct uart *uart;
    int rc;

    if (port >= UART_CNT) {
        return -1;
    }

    uart = &uarts[port];
    if (uart->u_open) {
        return -1;
    }
    uart->u_tx_func = tx_func;
    uart->u_tx_done = tx_done;
    uart->u_rx_func = rx_func;
    uart->u_func_arg = arg;
    uart->u_rx_char = -1;

    if (!uart_poller_running) {
        uart_poller_running = 1;
        rc = os_task_init(&uart_poller_task, "uartpoll", uart_poller, NULL,
                MYNEWT_VAL(MCU_UART_POLLER_PRIO), OS_WAIT_FOREVER, uart_poller_stack,
                UART_POLLER_STACK_SZ);
        assert(rc == 0);
    }
    return 0;
}

int
hal_uart_config(int port, int32_t baudrate, uint8_t databits, uint8_t stopbits,
                enum hal_uart_parity parity, enum hal_uart_flow_ctl flow_ctl)
{
    struct uart *uart;

    if (port >= UART_CNT) {
        return -1;
    }

    uart = &uarts[port];
    if (uart->u_open) {
        return -1;
    }

    if (native_uart_dev_strs[port] == NULL) {
        uart->u_fd = uart_pty(port);
    } else {
        uart->u_fd = uart_open_dev(port, baudrate, databits, stopbits,
                parity, flow_ctl);
    }

    if (uart->u_fd < 0) {
        return -1;
    }
    set_nonblock(uart->u_fd);


    uart_open_log();
    uart->u_open = 1;
    return 0;
}

int
hal_uart_close(int port)
{
    struct uart *uart;
    int rc;

    if (port >= UART_CNT) {
        rc = -1;
        goto err;
    }

    uart = &uarts[port];
    if (!uart->u_open) {
        rc = -1;
        goto err;
    }

    close(uart->u_fd);

    uart->u_open = 0;
    return (0);
    err:
    return (rc);
}

int
hal_uart_init(int port, void *arg)
{
    return (0);
}

int
uart_set_dev(int port, const char *dev_str)
{
    if (port < 0 || port >= UART_CNT) {
        return SYS_EINVAL;
    }

    if (uarts[port].u_open) {
        return SYS_EBUSY;
    }

    native_uart_dev_strs[port] = dev_str;

    return 0;
}
