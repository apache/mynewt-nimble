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

#include "mesh/glue.h"
#include "adv.h"

extern u8_t g_mesh_addr_type;

const char *
bt_hex(const void *buf, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    static char hexbufs[4][137];
    static u8_t curbuf;
    const u8_t *b = buf;
    char *str;
    int i;

    str = hexbufs[curbuf++];
    curbuf %= ARRAY_SIZE(hexbufs);

    len = min(len, (sizeof(hexbufs[0]) - 1) / 2);

    for (i = 0; i < len; i++) {
        str[i * 2] = hex[b[i] >> 4];
        str[i * 2 + 1] = hex[b[i] & 0xf];
    }

    str[i * 2] = '\0';

    return str;
}

void
net_buf_put(struct os_eventq *fifo, struct os_mbuf *om)
{
    struct os_event *ev;

    assert(OS_MBUF_IS_PKTHDR(om));
    ev = &BT_MESH_ADV(om)->ev;
    assert(ev);
    assert(ev->ev_arg);

    os_eventq_put(fifo, ev);
}

void *
net_buf_ref(struct os_mbuf *om)
{
    struct bt_mesh_adv *adv;

    /* For bufs with header we count refs*/
    if (OS_MBUF_USRHDR_LEN(om) == 0) {
        return om;
    }

    adv = BT_MESH_ADV(om);
    adv->ref_cnt++;

    return om;
}

void
net_buf_unref(struct os_mbuf *om)
{
    struct bt_mesh_adv *adv;

    /* For bufs with header we count refs*/
    if (OS_MBUF_USRHDR_LEN(om) == 0) {
        goto free;
    }

    adv = BT_MESH_ADV(om);
    if (--adv->ref_cnt > 0) {
        return;
    }

free:
    os_mbuf_free_chain(om);
}

int
bt_encrypt_be(const uint8_t *key, const uint8_t *plaintext, uint8_t *enc_data)
{
    struct tc_aes_key_sched_struct s;

    if (tc_aes128_set_encrypt_key(&s, key) == TC_CRYPTO_FAIL) {
        return BLE_HS_EUNKNOWN;
    }

    if (tc_aes_encrypt(enc_data, plaintext, &s) == TC_CRYPTO_FAIL) {
        return BLE_HS_EUNKNOWN;
    }

    return 0;
}

uint16_t
net_buf_simple_pull_le16(struct os_mbuf *om)
{
    uint16_t val;
    struct os_mbuf *old = om;

    om = os_mbuf_pullup(om, sizeof(val));
    assert(om == old);
    val = get_le16(om->om_data);
    os_mbuf_adj(om, sizeof(val));

    return val;
}

uint16_t
net_buf_simple_pull_be16(struct os_mbuf *om)
{
    uint16_t val;
    struct os_mbuf *old = om;

    om = os_mbuf_pullup(om, sizeof(val));
    assert(om == old);
    val = get_be16(om->om_data);
    os_mbuf_adj(om, sizeof(val));

    return val;
}

uint32_t
net_buf_simple_pull_be32(struct os_mbuf *om)
{
    uint32_t val;
    struct os_mbuf *old = om;

    om = os_mbuf_pullup(om, sizeof(val));
    assert(om == old);
    val = get_be32(om->om_data);
    os_mbuf_adj(om, sizeof(val));

    return val;
}

uint8_t
net_buf_simple_pull_u8(struct os_mbuf *om)
{
    uint8_t val;
    struct os_mbuf *old = om;

    om = os_mbuf_pullup(om, sizeof(val));
    assert(om == old);
    val = om->om_data[0];
    os_mbuf_adj(om, 1);

    return val;
}

void
net_buf_simple_add_le16(struct os_mbuf *om, uint16_t val)
{
    val = htole16(val);
    os_mbuf_append(om, &val, sizeof(val));
}

void
net_buf_simple_add_be16(struct os_mbuf *om, uint16_t val)
{
    val = htobe16(val);
    os_mbuf_append(om, &val, sizeof(val));
}

void
net_buf_simple_add_be32(struct os_mbuf *om, uint32_t val)
{
    val = htobe32(val);
    os_mbuf_append(om, &val, sizeof(val));
}

void
net_buf_simple_add_u8(struct os_mbuf *om, uint8_t val)
{
    os_mbuf_append(om, &val, 1);
}

void
net_buf_simple_push_le16(struct os_mbuf *om, uint16_t val)
{
    uint8_t headroom = om->om_data - &om->om_databuf[om->om_pkthdr_len];

    assert(headroom >= 2);
    om->om_data -= 2;
    put_le16(om->om_data, val);
    om->om_len += 2;

    if (om->om_pkthdr_len) {
        OS_MBUF_PKTHDR(om)->omp_len += 2;
    }
}

void
net_buf_simple_push_be16(struct os_mbuf *om, uint16_t val)
{
    uint8_t headroom = om->om_data - &om->om_databuf[om->om_pkthdr_len];

    assert(headroom >= 2);
    om->om_data -= 2;
    put_be16(om->om_data, val);
    om->om_len += 2;

    if (om->om_pkthdr_len) {
        OS_MBUF_PKTHDR(om)->omp_len += 2;
    }
}

void
net_buf_simple_push_u8(struct os_mbuf *om, uint8_t val)
{
    uint8_t headroom = om->om_data - &om->om_databuf[om->om_pkthdr_len];

    assert(headroom >= 1);
    om->om_data -= 1;
    om->om_data[0] = val;
    om->om_len += 1;

    if (om->om_pkthdr_len) {
        OS_MBUF_PKTHDR(om)->omp_len += 1;
    }
}

void
net_buf_add_zeros(struct os_mbuf *om, uint8_t len)
{
    uint8_t z[len];
    int rc;

    memset(z, 0, len);

    rc = os_mbuf_append(om, z, len);
    if(rc) {
        assert(0);
    }
}

void *
net_buf_simple_pull(struct os_mbuf *om, uint8_t len)
{
    os_mbuf_adj(om, len);
    return om->om_data;
}

void*
net_buf_simple_add(struct os_mbuf *om, uint8_t len)
{
    return os_mbuf_extend(om, len);
}

bool
k_fifo_is_empty(struct os_eventq *q)
{
    return STAILQ_EMPTY(&q->evq_list);
}

void * net_buf_get(struct os_eventq *fifo, s32_t t)
{
    struct os_event *ev = os_eventq_get_no_wait(fifo);

    if (ev) {
        return ev->ev_arg;
    }

    return NULL;
}

uint8_t *
net_buf_simple_push(struct os_mbuf *om, uint8_t len)
{
    uint8_t headroom = om->om_data - &om->om_databuf[om->om_pkthdr_len];

    assert(headroom >= len);
    om->om_data -= len;
    om->om_len += len;

    return om->om_data;
}

void
net_buf_reserve(struct os_mbuf *om, size_t reserve)
{
    /* We need reserve to be done on fresh buf */
    assert(om->om_len == 0);
    om->om_data += reserve;
}

void
k_work_init(struct os_callout *work, os_event_fn handler)
{
    os_callout_init(work, os_eventq_dflt_get(), handler, NULL);
}

void
k_delayed_work_init(struct k_delayed_work *w, os_event_fn *f)
{
    os_callout_init(&w->work, os_eventq_dflt_get(), f, NULL);
}

void
k_delayed_work_cancel(struct k_delayed_work *w)
{
    os_callout_stop(&w->work);
}

void
k_delayed_work_submit(struct k_delayed_work *w, uint32_t ms)
{
    uint32_t ticks;

    os_time_ms_to_ticks(ms, &ticks);
    os_callout_reset(&w->work, ticks);
}

void
k_work_submit(struct os_callout *w)
{
    os_callout_reset(w, 0);
}

void
k_work_add_arg(struct os_callout *w, void *arg)
{
    w->c_ev.ev_arg = arg;
}

void
k_delayed_work_add_arg(struct k_delayed_work *w, void *arg)
{
    w->work.c_ev.ev_arg = arg;
}

uint32_t
k_delayed_work_remaining_get (struct k_delayed_work *w)
{
    int sr;
    os_time_t t;

    OS_ENTER_CRITICAL(sr);

    t = os_callout_remaining_ticks(&w->work, os_cputime_get32());

    OS_EXIT_CRITICAL(sr);

    /* We should return ms */
    return os_cputime_ticks_to_usecs(t) / 1000;
}

int64_t k_uptime_get(void)
{
    /* We should return ms */
    return os_get_uptime_usec() / 1000;
}

u32_t k_uptime_get_32(void)
{
    return k_uptime_get();
}

static uint8_t pub[64];
static uint8_t priv[32];
static bool has_pub = false;

int
bt_dh_key_gen(const u8_t remote_pk[64], bt_dh_key_cb_t cb)
{
    uint8_t dh[32];

    if (ble_sm_alg_gen_dhkey((uint8_t *)&remote_pk[0], (uint8_t *)&remote_pk[32],
                              priv, dh)) {
        return -1;
    }

    cb(dh);
    return 0;
}

int
bt_rand(void *buf, size_t len)
{
    int rc;
    rc = ble_hs_hci_util_rand(buf, len);
    if (rc != 0) {
        return -1;
    }

    return 0;
}

int
bt_pub_key_gen(struct bt_pub_key_cb *new_cb)
{

    if (ble_sm_alg_gen_key_pair(pub, priv)) {
        assert(0);
        return -1;
    }

    new_cb->func(pub);
    has_pub = true;

    return 0;
}

uint8_t *
bt_pub_key_get(void)
{
    if (!has_pub) {
        return NULL;
    }

    return pub;
}

static int
set_ad(const struct bt_data *ad, size_t ad_len, u8_t *buf, u8_t *buf_len)
{
    int i;

    for (i = 0; i < ad_len; i++) {
        buf[(*buf_len)++] = ad[i].data_len + 1;
        buf[(*buf_len)++] = ad[i].type;

        memcpy(&buf[*buf_len], ad[i].data,
               ad[i].data_len);
        *buf_len += ad[i].data_len;
    }

    return 0;
}

int
bt_le_adv_start(const struct ble_gap_adv_params *param,
                const struct bt_data *ad, size_t ad_len,
                const struct bt_data *sd, size_t sd_len)
{
#if MYNEWT_VAL(BLE_EXT_ADV)
    uint8_t buf[MYNEWT_VAL(BLE_EXT_ADV_MAX_SIZE)];
#else
    uint8_t buf[BLE_HS_ADV_MAX_SZ];
#endif
    uint8_t buf_len = 0;
    int err;

    err = set_ad(ad, ad_len, buf, &buf_len);
    if (err) {
        return err;
    }

    err = ble_gap_adv_set_data(buf, buf_len);
    if (err != 0) {
        return err;
    }

    err = ble_gap_adv_start(g_mesh_addr_type, NULL, BLE_HS_FOREVER, param,
                            NULL, NULL);
    if (err) {
        BT_ERR("Advertising failed: err %d", err);
        return err;
    }

    return 0;
}

#if MYNEWT_VAL(BLE_MESH_PROXY)
int bt_mesh_proxy_svcs_register(void);
#endif

void
bt_mesh_register_gatt(void)
{
#if MYNEWT_VAL(BLE_MESH_PROXY)
    bt_mesh_proxy_svcs_register();
#endif
}
