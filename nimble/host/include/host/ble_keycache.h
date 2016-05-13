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
#ifndef H_BLE_KEYCACHE_
#define H_BLE_KEYCACHE_

struct ble_gap_key_parms;

/*
 * This is a helper library to help the application store and access
 * keys for devices.  The keys are passed from the stack as a single
 * blob in ble_gap_key_parms.  When this is written to the cache, it
 * will overwrite any previous data for that address,  So, if you
 * want to preserved any information from a previous write for this
 * address, you had better read the value out first and then
 * preserve what you want.
 *
 * A note on address types.  This cache will store any address type.
 * Its up to the application to filter the address and determine
 * whether its worth storing in the keycache or not
 *
 * A note on multi-tasking.  This library is written to be access from a single
 * task.  If you intend to use this from multiple tasks (e.g. a UI task to
 * delete bonding data and the host task to add bondind data) its up to your
 * application to provide mutual exclusion
 */

/**
 * Adds an entry to the LTK keycache.
 * Returns an error (negative) if the LTK key cache is full .
 * Within the key params struct, addr must be valid as this is
 * the lookup for key cache entries.
 */
int
ble_keycache_add(uint8_t addr_type, uint8_t *key_addr, struct ble_gap_key_parms *pltk);

/*
 * reads values for the given addr from the keycache.  If no entry
 * is found, a negative value is returned and pout_entry is undefined.
 */
int
ble_keycache_find(uint8_t addr_type, uint8_t *key_addr, struct ble_gap_key_parms *pout_entry);

/*
 * Deletes an entry from the keycache for the give address.
 * Returns 0 if the entry is successfully deleted.  NOTE: if there is
 * no entry for the given address the function will return 0.  Returns
 * negative on error
 * @Param addr -- a 6 octet mac address of the device
 */
 int
 ble_keycache_delete(uint8_t addr_type, uint8_t *key_addr);

/* Initialize a key-cache.
 */

int
ble_keycache_init(int max_entries);

#endif /* H_BLE_KEYCACHE_ */
