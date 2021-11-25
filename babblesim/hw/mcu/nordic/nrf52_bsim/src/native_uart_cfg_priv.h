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

#ifndef H_NATIVE_UART_CFG_PRIV_
#define H_NATIVE_UART_CFG_PRIV_

#include <termios.h>
#include "hal/hal_uart.h"

speed_t uart_baud_to_speed(int_least32_t baud);
int uart_dev_set_attr(int fd, int32_t baudrate, uint8_t databits,
                      uint8_t stopbits, enum hal_uart_parity parity,
                      enum hal_uart_flow_ctl flow_ctl);

#endif
