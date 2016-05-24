/**
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

#ifndef H_BLE_HS_TEST_UTIL_STORE_
#define H_BLE_HS_TEST_UTIL_STORE_

union ble_store_value;
union ble_store_key;

extern int ble_hs_test_util_store_num_our_ltks;
extern int ble_hs_test_util_store_num_peer_ltks;
extern int ble_hs_test_util_store_num_cccds;

void ble_hs_test_util_store_init(int max_our_ltks, int max_peer_ltks,
                                 int max_cccds);
int ble_hs_test_util_store_read(int obj_type, union ble_store_key *key,
                                union ble_store_value *dst);
int ble_hs_test_util_store_write(int obj_type, union ble_store_value *value);

#endif
