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
#include <controller/ble_fem.h>
#include "phy_priv.h"

/*
 * When the radio is operated on high voltage (see VREQCTRL - Voltage request
 * control on page 62 for how to control voltage), the output power is increased
 * by 3 dB. I.e. if the TXPOWER value is set to 0 dBm and high voltage is
 * requested using VREQCTRL, the output power will be +3
 * */
#define NRF_TXPOWER_VREQH 3

#if PHY_USE_DEBUG
void
phy_debug_init(void)
{
}
#endif /* PHY_USE_DEBUG */

#if PHY_USE_FEM
void
phy_fem_init()
{
}

#if PHY_USE_FEM_PA
void
phy_fem_enable_pa(void)
{

}
#endif

#if PHY_USE_FEM_LNA
void
phy_fem_enable_lna(void)
{

}
#endif

void
phy_fem_disable(void)
{
}
#endif /* PHY_USE_FEM */

void
phy_ppi_init(void)
{
}

void
phy_txpower_set(int8_t dbm)
{
}

int8_t
phy_txpower_round(int8_t dbm)
{
    return (int8_t)RADIO_TXPOWER_TXPOWER_Neg40dBm;
}
