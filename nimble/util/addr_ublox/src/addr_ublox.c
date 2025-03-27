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
#include <nrfx.h>

int
ble_ll_addr_provide_public(uint8_t *addr)
{
    uint32_t addr_high;
    uint32_t addr_low;

    /* The u-blox modules are preprogrammed from the factory with a unique public
     * device address. The address is stored in the CUSTOMER[0] and CUSTOMER[1]
     * registers of the UICR in little endian format. The most significant bytes
     * of the CUSTOMER[1] register are 0xFF to complete the 32-bit register value.
     *
     * The Bluetooth device address consists of the registered OUI combined with
     * the hexadecimal digits printed on a 2D barcode and as a human-readable
     * text on the module label.
     */

    addr_low = NRF_UICR->CUSTOMER[0];
    addr_high = NRF_UICR->CUSTOMER[1];

    memcpy(&addr[0], &addr_low, 4);
    memcpy(&addr[4], &addr_high, 2);

    return 0;
}
