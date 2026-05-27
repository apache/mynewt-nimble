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
#include <nimble/ble.h>
#include <controller/ble_ll_crypto.h>
#include <controller/ble_hw.h>
#include <mbedtls/cmac.h>

int
ble_ll_crypto_cmac(const uint8_t *key, const uint8_t *in, int len,
                   uint8_t *out)
{
    if (mbedtls_cipher_cmac(mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB),
                            key, 128, in, len, out)) {
        return -1;
    }

    return 0;
}

int
ble_ll_crypto_h8(const uint8_t *k, const uint8_t *s, const uint8_t *key_id,
                 uint8_t *out)
{
    uint8_t ik[16];
    int rc;

    rc = ble_ll_crypto_cmac(s, k, 16, ik);
    if (rc) {
        return rc;
    }

    return ble_ll_crypto_cmac(ik, key_id, 4, out);
}
