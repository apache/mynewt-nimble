/*
 * Copyright (c) 2015 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* These functions are adapted from the Intel Zephyr BLE security manager
 * code.
 */

#include <inttypes.h>
#include <string.h>
#include "mbedtls/aes.h"
#include "mbedtls/cmac.h"
#include "mbedtls/ecdsa.h"
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "ble_hs_priv.h"

#if NIMBLE_OPT(SM)

static mbedtls_aes_context ble_sm_alg_ctxt;
//static mbedtls_ecdh_context ble_sm_alg_ecdh_ctx;
//static mbedtls_cmac_context ble_sm_alg_cmac_ctxt;

/* based on Core Specification 4.2 Vol 3. Part H 2.3.5.6.1 */
static const uint32_t ble_sm_alg_dbg_priv_key[8] = {
    0xcd3c1abd, 0x5899b8a6, 0xeb40b799, 0x4aff607b, 0xd2103f50, 0x74c9b3e3,
    0xa3c55f38, 0x3f49f6d4
};

static const uint8_t ble_sm_alg_dbg_pub_key[64] = {
    0xe6, 0x9d, 0x35, 0x0e, 0x48, 0x01, 0x03, 0xcc,
    0xdb, 0xfd, 0xf4, 0xac, 0x11, 0x91, 0xf4, 0xef,
    0xb9, 0xa5, 0xf9, 0xe9, 0xa7, 0x83, 0x2c, 0x5e,
    0x2c, 0xbe, 0x97, 0xf2, 0xd2, 0x03, 0xb0, 0x20,
    0x8b, 0xd2, 0x89, 0x15, 0xd0, 0x8e, 0x1c, 0x74,
    0x24, 0x30, 0xed, 0x8f, 0xc2, 0x45, 0x63, 0x76,
    0x5c, 0x15, 0x52, 0x5a, 0xbf, 0x9a, 0x32, 0x63,
    0x6d, 0xeb, 0x2a, 0x65, 0x49, 0x9c, 0x80, 0xdc
};

static const uint8_t ble_sm_alg_dbg_f4[16] = {
    0x2d, 0x87, 0x74, 0xa9, 0xbe, 0xa1, 0xed, 0xf1,
    0x1c, 0xbd, 0xa9, 0x07, 0xf1, 0x16, 0xc9, 0xf2,
};

static const uint8_t ble_sm_alg_dbg_f5[32] = {
    0x20, 0x6e, 0x63, 0xce, 0x20, 0x6a, 0x3f, 0xfd,
    0x02, 0x4a, 0x08, 0xa1, 0x76, 0xf1, 0x65, 0x29,
    0x38, 0x0a, 0x75, 0x94, 0xb5, 0x22, 0x05, 0x98,
    0x23, 0xcd, 0xd7, 0x69, 0x11, 0x79, 0x86, 0x69,
};

static const uint8_t ble_sm_alg_dbg_f6[16] = {
    0x61, 0x8f, 0x95, 0xda, 0x09, 0x0b, 0x6c, 0xd2,
    0xc5, 0xe8, 0xd0, 0x9c, 0x98, 0x73, 0xc4, 0xe3,
};

#if 0
static void
ble_sm_alg_log_buf(char *name, uint8_t *buf, int len)
{
    BLE_HS_LOG(DEBUG, "    %s=", name);
    ble_hs_misc_log_flat_buf(buf, len);
    BLE_HS_LOG(DEBUG, "\n");
}
#endif

static void
ble_sm_alg_xor_128(uint8_t *p, uint8_t *q, uint8_t *r)
{
    int i;

    for (i = 0; i < 16; i++) {
        r[i] = p[i] ^ q[i];
    }
}

static int
ble_sm_alg_encrypt(uint8_t *key, uint8_t *plaintext, uint8_t *enc_data)
{
    mbedtls_aes_init(&ble_sm_alg_ctxt);
    uint8_t tmp[16];
    int rc;

    swap_buf(tmp, key, 16);

    rc = mbedtls_aes_setkey_enc(&ble_sm_alg_ctxt, tmp, 128);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    swap_buf(tmp, plaintext, 16);

    rc = mbedtls_aes_crypt_ecb(&ble_sm_alg_ctxt, MBEDTLS_AES_ENCRYPT,
                               tmp, enc_data);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    swap_in_place(enc_data, 16);

    return 0;
}

int
ble_sm_alg_s1(uint8_t *k, uint8_t *r1, uint8_t *r2, uint8_t *out)
{
    int rc;

    /* The most significant 64-bits of r1 are discarded to generate
     * r1' and the most significant 64-bits of r2 are discarded to
     * generate r2'.
     * r1' is concatenated with r2' to generate r' which is used as
     * the 128-bit input parameter plaintextData to security function e:
     *
     *    r' = r1' || r2'
     */
    memcpy(out, r2, 8);
    memcpy(out + 8, r1, 8);

    /* s1(k, r1 , r2) = e(k, r') */
    rc = ble_sm_alg_encrypt(k, out, out);
    if (rc != 0) {
        return rc;
    }

    BLE_HS_LOG(DEBUG, "ble_sm_alg_s1()\n    k=");
    ble_hs_misc_log_flat_buf(k, 16);
    BLE_HS_LOG(DEBUG, "\n    r1=");
    ble_hs_misc_log_flat_buf(r1, 16);
    BLE_HS_LOG(DEBUG, "\n    r2=");
    ble_hs_misc_log_flat_buf(r2, 16);
    BLE_HS_LOG(DEBUG, "\n    out=");
    ble_hs_misc_log_flat_buf(out, 16);
    BLE_HS_LOG(DEBUG, "\n");

    return 0;
}

int
ble_sm_alg_c1(uint8_t *k, uint8_t *r,
              uint8_t *preq, uint8_t *pres,
              uint8_t iat, uint8_t rat,
              uint8_t *ia, uint8_t *ra,
              uint8_t *out_enc_data)
{
    uint8_t p1[16], p2[16];
    int rc;

    BLE_HS_LOG(DEBUG, "ble_sm_alg_c1()\n    k=");
    ble_hs_misc_log_flat_buf(k, 16);
    BLE_HS_LOG(DEBUG, "\n    r=");
    ble_hs_misc_log_flat_buf(r, 16);
    BLE_HS_LOG(DEBUG, "\n    iat=%d rat=%d", iat, rat);
    BLE_HS_LOG(DEBUG, "\n    ia=");
    ble_hs_misc_log_flat_buf(ia, 6);
    BLE_HS_LOG(DEBUG, "\n    ra=");
    ble_hs_misc_log_flat_buf(ra, 6);
    BLE_HS_LOG(DEBUG, "\n    preq=");
    ble_hs_misc_log_flat_buf(preq, 7);
    BLE_HS_LOG(DEBUG, "\n    pres=");
    ble_hs_misc_log_flat_buf(pres, 7);

    /* pres, preq, rat and iat are concatenated to generate p1 */
    p1[0] = iat;
    p1[1] = rat;
    memcpy(p1 + 2, preq, 7);
    memcpy(p1 + 9, pres, 7);

    BLE_HS_LOG(DEBUG, "\n    p1=");
    ble_hs_misc_log_flat_buf(p1, sizeof p1);

    /* c1 = e(k, e(k, r XOR p1) XOR p2) */

    /* Using out_enc_data as temporary output buffer */
    ble_sm_alg_xor_128(r, p1, out_enc_data);

    rc = ble_sm_alg_encrypt(k, out_enc_data, out_enc_data);
    if (rc != 0) {
        rc = BLE_HS_EUNKNOWN;
        goto done;
    }

    /* ra is concatenated with ia and padding to generate p2 */
    memcpy(p2, ra, 6);
    memcpy(p2 + 6, ia, 6);
    memset(p2 + 12, 0, 4);

    BLE_HS_LOG(DEBUG, "\n    p2=");
    ble_hs_misc_log_flat_buf(p2, sizeof p2);

    ble_sm_alg_xor_128(out_enc_data, p2, out_enc_data);

    rc = ble_sm_alg_encrypt(k, out_enc_data, out_enc_data);
    if (rc != 0) {
        rc = BLE_HS_EUNKNOWN;
        goto done;
    }

    BLE_HS_LOG(DEBUG, "\n    out_enc_data=");
    ble_hs_misc_log_flat_buf(out_enc_data, 16);

    rc = 0;

done:
    BLE_HS_LOG(DEBUG, "\n    rc=%d\n", rc);
    return rc;
}

int
ble_sm_alg_f4(uint8_t *u, uint8_t *v, uint8_t *x, uint8_t z,
              uint8_t *out_enc_data)
{
#if 0
    uint8_t xs[16];
    uint8_t m[65];
    int rc;

    BLE_HS_LOG(DEBUG, "ble_sm_alg_f4()\n    u=");
    ble_hs_misc_log_flat_buf(u, 32);
    BLE_HS_LOG(DEBUG, "\n    v=");
    ble_hs_misc_log_flat_buf(v, 32);
    BLE_HS_LOG(DEBUG, "\n    x=");
    ble_hs_misc_log_flat_buf(x, 16);
    BLE_HS_LOG(DEBUG, "\n    z=0x%02x\n", z);

    mbedtls_cmac_init(&ble_sm_alg_cmac_ctxt);

    /*
     * U, V and Z are concatenated and used as input m to the function
     * AES-CMAC and X is used as the key k.
     *
     * Core Spec 4.2 Vol 3 Part H 2.2.5
     *
     * note:
     * XXX bt_smp_aes_cmac uses BE data and smp_f4 accept LE so we swap
     */
    swap_buf(m, u, 32);
    swap_buf(m + 32, v, 32);
    m[64] = z;

    swap_buf(xs, x, 16);

    rc = mbedtls_aes_cmac_prf_128(&ble_sm_alg_cmac_ctxt, xs, sizeof xs,
                                  m, sizeof m, out_enc_data);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    swap_in_place(out_enc_data, 16);

    BLE_HS_LOG(DEBUG, "    out_enc_data=");
    ble_hs_misc_log_flat_buf(out_enc_data, 16);
    BLE_HS_LOG(DEBUG, "\n");
#endif

    memcpy(out_enc_data, ble_sm_alg_dbg_f4,
           sizeof ble_sm_alg_dbg_f4);
    return 0;
}

int
ble_sm_alg_g2(uint8_t *u, uint8_t *v, uint8_t *x, uint8_t *y,
              uint32_t *passkey)
{
#if 0
    uint8_t m[80], xs[16];
    int rc;

    ble_sm_alg_log_buf("u", u, 32);
    ble_sm_alg_log_buf("v", v, 32);
    ble_sm_alg_log_buf("x", x, 16);
    ble_sm_alg_log_buf("y", y, 16);

    mbedtls_cmac_init(&ble_sm_alg_cmac_ctxt);

    swap_buf(m, u, 32);
    swap_buf(m + 32, v, 32);
    swap_buf(m + 64, y, 16);

    swap_buf(xs, x, 16);

    /* reuse xs (key) as buffer for result */
    rc = mbedtls_aes_cmac_prf_128(&ble_sm_alg_cmac_ctxt, xs, sizeof xs,
                                  m, sizeof m, xs);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    ble_sm_alg_log_buf("res", xs, 16);

    *passkey = be32toh(xs + 12) % 1000000;
    BLE_HS_LOG(DEBUG, "    passkey=%u", *passkey);
#endif

    return 0;
}

int
ble_sm_alg_f5(uint8_t *w, uint8_t *n1, uint8_t *n2, uint8_t a1t,
              uint8_t *a1, uint8_t a2t, uint8_t *a2, uint8_t *mackey,
              uint8_t *ltk)
{
#if 0
    static const uint8_t salt[16] = { 0x6c, 0x88, 0x83, 0x91, 0xaa, 0xf5,
                      0xa5, 0x38, 0x60, 0x37, 0x0b, 0xdb,
                      0x5a, 0x60, 0x83, 0xbe };
    uint8_t m[53] = {
        0x00, /* counter */
        0x62, 0x74, 0x6c, 0x65, /* keyID */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*n1*/
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*2*/
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* a1 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* a2 */
        0x01, 0x00 /* length */
    };
    uint8_t ws[32];
    uint8_t t[16];
    int rc;

    ble_sm_alg_log_buf("w", w, 32);
    ble_sm_alg_log_buf("n1", n1, 16);
    ble_sm_alg_log_buf("n2", n2, 16);

    mbedtls_cmac_init(&ble_sm_alg_cmac_ctxt);

    swap_buf(ws, w, 32);

    rc = mbedtls_aes_cmac_prf_128(&ble_sm_alg_cmac_ctxt,
                                  salt, sizeof salt, ws, sizeof ws, t);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    ble_sm_alg_log_buf("t", t, 16);

    swap_buf(m + 5, n1, 16);
    swap_buf(m + 21, n2, 16);
    m[37] = a1t;
    swap_buf(m + 38, a1, 6);
    m[44] = a2t;
    swap_buf(m + 45, a2, 6);

    rc = mbedtls_aes_cmac_prf_128(&ble_sm_alg_cmac_ctxt,
                                  t, sizeof t, m, sizeof m, mackey);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    ble_sm_alg_log_buf("mackey", mackey, 16);

    swap_in_place(mackey, 16);

    /* counter for ltk is 1 */
    m[0] = 0x01;

    rc = mbedtls_aes_cmac_prf_128(&ble_sm_alg_cmac_ctxt,
                                  t, sizeof t, m, sizeof m, ltk);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    ble_sm_alg_log_buf("ltk", ltk, 16);

    swap_in_place(ltk, 16);
#endif

    memcpy(mackey, ble_sm_alg_dbg_f5 + 16, 16);
    memcpy(ltk, ble_sm_alg_dbg_f5, 16);
    return 0;
}

int
ble_sm_alg_f6(uint8_t *w, uint8_t *n1, uint8_t *n2, uint8_t *r,
              uint8_t *iocap, uint8_t a1t, uint8_t *a1,
              uint8_t a2t, uint8_t *a2, uint8_t *check)
{
#if 0
    uint8_t ws[16];
    uint8_t m[65];
    int rc;

    ble_sm_alg_log_buf("w", w, 16);
    ble_sm_alg_log_buf("n1", n1, 16);
    ble_sm_alg_log_buf("n2", n2, 16);
    ble_sm_alg_log_buf("r", r, 16);
    ble_sm_alg_log_buf("iocap", iocap, 3);
    ble_sm_alg_log_buf("a1t", &a1t, 1);
    ble_sm_alg_log_buf("a1", a1, 6);
    ble_sm_alg_log_buf("a2t", &a2t, 1);
    ble_sm_alg_log_buf("a2", a2, 6);

    mbedtls_cmac_init(&ble_sm_alg_cmac_ctxt);

    swap_buf(m, n1, 16);
    swap_buf(m + 16, n2, 16);
    swap_buf(m + 32, r, 16);
    swap_buf(m + 48, iocap, 3);

    m[51] = a1t;
    memcpy(m + 52, a1, 6);
    swap_buf(m + 52, a1, 6);

    m[58] = a2t;
    memcpy(m + 59, a2, 6);
    swap_buf(m + 59, a2, 6);

    swap_buf(ws, w, 16);

    rc = mbedtls_aes_cmac_prf_128(&ble_sm_alg_cmac_ctxt,
                                  ws, sizeof ws, m, sizeof m, check);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    ble_sm_alg_log_buf("res", check, 16);

    swap_in_place(check, 16);
#endif

    memcpy(check, ble_sm_alg_dbg_f6, sizeof ble_sm_alg_dbg_f6);
    return 0;
}

/**
 * Passed to mbedtls ecc function.
 */
#if 0
static int
ble_sm_alg_rnd(void *arg, unsigned char *dst, size_t len)
{
    // XXX
    return 0;
}
#endif

/**
 * pub: 64 bytes
 * priv: 32 bytes
 */
int
ble_sm_alg_gen_key_pair(uint8_t *pub, uint8_t *priv)
{
    memcpy(pub, ble_sm_alg_dbg_pub_key, sizeof ble_sm_alg_dbg_pub_key);
    memcpy(priv, ble_sm_alg_dbg_priv_key, sizeof ble_sm_alg_dbg_priv_key);

    return 0;
}

#endif
