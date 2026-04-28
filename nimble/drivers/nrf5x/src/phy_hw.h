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
 * MCU-specific PHY hardware abstraction dispatcher.
 * Routes to the correct chip-specific phy_hw.h based on MCU_TARGET.
 */

#ifndef H_PHY_HW_DISPATCH_
#define H_PHY_HW_DISPATCH_

#include "syscfg/syscfg.h"

#if MYNEWT_VAL_CHOICE(MCU_TARGET, nRF54L15) || \
    MYNEWT_VAL_CHOICE(MCU_TARGET, nRF54L10) || \
    MYNEWT_VAL_CHOICE(MCU_TARGET, nRF54L05)
#include "nrf54l/phy_hw.h"
#elif MYNEWT_VAL_CHOICE(MCU_TARGET, nRF5340_NET)
#include "nrf53/phy_hw.h"
#else
#include "nrf52/phy_hw.h"
#endif

#endif /* H_PHY_HW_DISPATCH_ */
