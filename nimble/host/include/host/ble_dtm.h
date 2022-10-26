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

#ifndef H_BLE_DTM_
#define H_BLE_DTM_

#include <inttypes.h>
#ifdef __cplusplus
extern "C" {
#endif

struct ble_dtm_rx_params {
    uint8_t channel;
    uint8_t phy;
    uint8_t modulation_index;
};

int ble_dtm_rx_start(const struct ble_dtm_rx_params *params);

struct ble_dtm_tx_params {
    uint8_t channel;
    uint8_t test_data_len;
    uint8_t payload;
    uint8_t phy;
};
int ble_dtm_tx_start(const struct ble_dtm_tx_params *params);

int ble_dtm_stop(uint16_t *num_packets);

#ifdef __cplusplus
}
#endif
#endif
