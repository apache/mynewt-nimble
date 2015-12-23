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

#include "ble_hs_priv.h"

static struct ble_hs_cfg ble_hs_cfg_dflt = {
    .max_connections = 16,
    .max_outstanding_pkts_per_conn = 5,
};

struct ble_hs_cfg ble_hs_cfg;

void
ble_hs_cfg_init(void)
{
    ble_hs_cfg = ble_hs_cfg_dflt;
}
