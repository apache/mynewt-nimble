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

#ifndef  BLE_ATT_CACHING_H_
#define BLE_ATT_CACHING_H_

/**
 * @brief Attribute caching is an optimization that allows the client to discover the
 * @         Attribute information such as Attribute Handles used by the server once and
 * @         use the same Attribute information across re-connections without re-discovery.
 * @ingroup bt_att_caching
 * @defgroup bt_host
 * @{
 */

#include "syscfg/syscfg.h"
#include "os/queue.h"
#include "host/ble_gap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_ATT_CASHING_MAX_SVCS               32
#define BLE_ATT_CASHING_MAX_CHRS               64
#define BLE_ATT_CASHING_MAX_DSCS               64
#define BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16     0x2a05

/**
 * Add new attribute connection for caching
 *
 * Add new connection till the max number of connection is reached,
 * save connection handle and peer Identity address and initialize the list
 * of services per connection.
 * This function Must be called one time for the same device over re-connections.
 *
 * @param desc      A pointer to the current connection descriptor
 *
 * @return              Attribute caching connection structure
 */
struct ble_att_caching_conn *
ble_att_caching_conn_add(struct ble_gap_conn_desc *desc);

/**
 * Add service in order in the same connection.
 *
 * Add the discovered service to the cached list for the same connection,
 * if the service is already found in the list return it,
 * else create a new one from ble_att_caching_svc_pool.
 *
 * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
 * @param gatt_svc             The service to be cached
 *
 * @return               0  on success
 *                      BLE_HS_ENOMEM if out of memory, no more memory pools
 *                      BLE_HS_ENOTCONN if no matching connection was
 *                  found.
 */
int
ble_att_caching_svc_add(uint16_t conn_handle,
        const struct ble_gatt_svc *gatt_svc);

/**
 * Add characteristic in order in the same connection.
 *
 * Add the discovered characteristic to the cached list for the same service,
 * if the characteristic is already found in the list return it,
 * else create a new one from ble_att_caching_chr_pool.
 *
 * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
 * @param svc_start_handle      start handle in the service range
 * @param gatt_chr             The characteristic to be cached
 *
 * @return               0  on success
 *                      BLE_HS_ENOMEM if out of memory, no more memory pools
 *                      BLE_HS_ENOTCONN if no matching connection was
 *                  found
 *                      BLE_HS_EINVAL if no list to add the char to.
 */
int
ble_att_caching_chr_add(uint16_t conn_handle, uint16_t svc_start_handle,
        const struct ble_gatt_chr *gatt_chr);

/**
 * Add descriptor in order in the same connection.
 *
 * Add the discovered descriptor to the cached list for the same characteristic,
 * if the descriptor is already found in the list return it,
 * else create a new one from ble_att_caching_dsc_pool.
 *
 * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
 * @param chr_val_handle  characteristic value handle
 * @param gatt_chr             The descriptor to be cached
 *
 * @return               0  on success
 *                      BLE_HS_ENOMEM if out of memory, no more memory pools
 *                      BLE_HS_ENOTCONN if no matching connection was
 *                  found
 *                      BLE_HS_EINVAL if no list to add the dsc to.
 */
int
ble_att_caching_dsc_add(uint16_t conn_handle, uint16_t chr_val_handle,
        const struct ble_gatt_dsc *gatt_dsc);

/**
 * After discover all services this function must be called to indicate att_caching
 * that all services are already cached.
 *
 * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
 *
 * @return               0  on success
 *                      BLE_HS_ENOTCONN if no matching connection was
 *                  found. */
int
ble_att_caching_set_all_services_cached(uint16_t conn_handle);

/**
 * After discover all characteristics this function must be called to indicate att_caching
 * that all characteristics are already cached for a specific service.
 *
 * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
 * @param start_handle     start handle in the service range
 *
 * @return               0  on success
 *                      BLE_HS_ENOTCONN if no matching connection was
 *                  found
 *                      BLE_HS_EINVAL if no list to add the char to.
 */
int
ble_att_caching_set_all_chrs_cached(uint16_t conn_handle, uint16_t start_handle);

/**
 * After discover all descriptors this function must be called to indicate att_caching
 * that all descriptors are already cached for a specific characteristic.
 *
 * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
 * @param chr_val_handle  characteristic value handle
 *
 * @return               0  on success
 *                      BLE_HS_ENOTCONN if no matching connection was
 *                  found
 *                      BLE_HS_EINVAL if no list to add the char to.
 */
int
ble_att_caching_set_all_dscs_cached(uint16_t conn_handle, uint16_t chr_val_handle);

/**
 * Check if we need to discover server's attribute (services) or the server had a bond with this
 * client, the client cached all these attribute and this is the same server address from the
 * previous connection.
 *
 * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
 *
 * @return               0  don't discover as all services are already cached
 *                      BLE_HS_ENOTCONN if no matching connection was
 *                  found
 *                      BLE_HS_EINVAL We need to discover as of the three concisions is violated.
 */
int
ble_att_caching_check_if_services_cached(uint16_t conn_handle);

/**
 * If all services are already cached, we will not discover on air we need to
 * notify the app with all cached services.
 *
 * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 * @return              Void
 */
void
ble_att_caching_notify_application_with_cached_services(uint16_t conn_handle,
        ble_gatt_disc_svc_fn *cb, void *cb_arg);

/**
 * Check if we need to discover server's attribute (characteristics) or the server had a bond with this
 * client, the client cached all these attribute and this is the same server address from the
 * previous connection.
 * If all characteristics are already cached, we will not discover on air we need to
 * notify the app with all cached chars.
 *
 * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return               0  don't discover as all services are already cached
 *                      BLE_HS_ENOTCONN if no matching connection was
 *                  found
 *                      BLE_HS_EINVAL We need to discover as of the three concisions is violated.
 */
int
ble_att_caching_check_if_chars_cached(uint16_t conn_handle,
        uint16_t start_handle, ble_gatt_chr_fn *cb, void *cb_arg);

/**
 * Check if we need to discover server's attribute (descriptors) or the server had a bond with this
 * client, the client cached all these attribute and this is the same server address from the
 * previous connection.
 * If all descriptors are already cached, we will not discover on air we need to
 * notify the app with all cached chars.
 *
 * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
  * @param chr_val_handle      characteristic value handle
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return               0  don't discover as all services are already cached
 *                      BLE_HS_ENOTCONN if no matching connection was
 *                  found
 *                      BLE_HS_EINVAL We need to discover as of the three concisions is violated.
 */
int
ble_att_caching_check_if_dsc_cached(uint16_t conn_handle,
        uint16_t chr_val_handle, ble_gatt_dsc_fn *cb, void *cb_arg);

/**
 * Get the attribute handle stored for service changed characteristic.
 *
 * * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
 *
 * @return              service changed attribute handle
 */
uint16_t
ble_att_caching_get_service_changed_att_handle(uint16_t conn_handle);

/**
 * When receive indication the a specific range of attribute is changed, delete all stored
 * attribute in attribute caching.
 *
 * @param conn_handle      connection handle for the current connection
 *                                        from the gap descriptor
 * @param start_handle  The start Attribute Handle shall be the start Attribute Handle of
 *                                         the service definition containing the change
 * @param end_handle   The end Attribute Handle shall
 *                                         be the last Attribute Handle of the service definition
 *                                         containing the change.
 *
 * @return               0  on success
 *                      BLE_HS_ENOTCONN if no matching connection was
 *                  found
 *                      BLE_HS_EINVAL service is not found with that start handle.
 */
int
ble_att_caching_service_changed_rx(uint16_t conn_handle, uint16_t start_handle,
        uint16_t end_handle);

/**
 * Initialize services, characteristics and descriptors memory pools.
 *
 * @return            0  on success
 */
int
ble_att_caching_init(void);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* BLE_ATT_CACHING_H_ */
