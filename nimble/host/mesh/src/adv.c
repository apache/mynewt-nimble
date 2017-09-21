/*  Bluetooth Mesh */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mesh/mesh.h"

#include "syscfg/syscfg.h"
#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG_ADV))
#include "host/ble_hs_log.h"

#include "host/ble_hs_adv.h"
#include "host/ble_gap.h"
#include "nimble/hci_common.h"

#include "adv.h"
#include "foundation.h"
#include "net.h"
#include "beacon.h"
#include "prov.h"
#include "proxy.h"

/* Window and Interval are equal for continuous scanning */
#define MESH_SCAN_INTERVAL 0x10
#define MESH_SCAN_WINDOW   0x10

/* Convert from ms to 0.625ms units */
#define ADV_INT(_ms) ((_ms) * 8 / 5)

/* Pre-5.0 controllers enforce a minimum interval of 100ms
 * whereas 5.0+ controllers can go down to 20ms.
 */
#define ADV_INT_DEFAULT  K_MSEC(100)
#define ADV_INT_FAST     K_MSEC(20)

/* TinyCrypt PRNG consumes a lot of stack space, so we need to have
 * an increased call stack whenever it's used.
 */
#define ADV_STACK_SIZE 768

struct os_task adv_task;
static struct os_eventq adv_queue;
extern u8_t g_mesh_addr_type;

static os_membuf_t adv_buf_mem[OS_MEMPOOL_SIZE(
        MYNEWT_VAL(BLE_MESH_ADV_BUF_COUNT),
        BT_MESH_ADV_DATA_SIZE + BT_MESH_ADV_USER_DATA_SIZE)];

struct os_mbuf_pool adv_os_mbuf_pool;
static struct os_mempool adv_buf_mempool;

static const u8_t adv_type[] = {
    [BT_MESH_ADV_PROV] = BLE_HS_ADV_TYPE_MESH_PROV,
    [BT_MESH_ADV_DATA] = BLE_HS_ADV_TYPE_MESH_MESSAGE,
    [BT_MESH_ADV_BEACON] = BLE_HS_ADV_TYPE_MESH_BEACON,
};


static inline void adv_sent(struct os_mbuf *buf, int err)
{
	if (BT_MESH_ADV(buf)->busy) {
		BT_MESH_ADV(buf)->busy = 0;

		if (BT_MESH_ADV(buf)->sent) {
			BT_MESH_ADV(buf)->sent(buf, err);
		}
	}

	net_buf_unref(buf);
}

static inline void adv_send(struct os_mbuf *buf)
{
    /* XXX: For BT5 we could have better adv interval */
    const s32_t adv_int_min =  ADV_INT_DEFAULT;
    struct ble_gap_adv_params param = { 0 };
    u16_t duration, adv_int;
    struct bt_mesh_adv *adv = BT_MESH_ADV(buf);
    struct bt_data ad;
    int err;

    adv_int = max(adv_int_min, adv->adv_int);
    duration = (adv->count + 1) * (adv_int + 10);

    BT_DBG("buf %p, type %u len %u:", buf, adv->type,
           buf->om_len);
    BT_DBG("count %u interval %ums duration %ums",
               adv->count + 1, adv_int, duration);

    ad.type = adv_type[BT_MESH_ADV(buf)->type];
    ad.data_len = buf->om_len;
    ad.data = buf->om_data;

    param.itvl_min = ADV_INT(adv_int);
    param.itvl_max = param.itvl_min;
    param.conn_mode = BLE_GAP_CONN_MODE_NON;

    err = bt_le_adv_start(&param, &ad, 1, NULL, 0);
    adv_sent(buf, err);
    if (err) {
        BT_ERR("Advertising failed: err %d", err);
        return;
    }

    BT_DBG("Advertising started. Sleeping %u ms", duration);

    os_time_delay(OS_TICKS_PER_SEC * duration / 1000);

    err = bt_le_adv_stop();
    if (err) {
        BT_ERR("Stopping advertising failed: err %d", err);
        return;
    }

    BT_DBG("Advertising stopped");
}

static void
adv_thread(void *args)
{
    static struct os_event *ev;
    struct os_mbuf *adv_data;
    struct bt_mesh_adv *adv;
#if (MYNEWT_VAL(BLE_MESH_PROXY))
    s32_t timeout;
    struct os_eventq *eventq_pool = &adv_queue;
#endif

    BT_DBG("started");

    while (1) {
#if (MYNEWT_VAL(BLE_MESH_PROXY))
        ev = os_eventq_get_no_wait(&adv_queue);
        while (!ev) {
            timeout = bt_mesh_proxy_adv_start();
            BT_DBG("Proxy Advertising up to %d ms", timeout);

            // FIXME: should we redefine K_SECONDS macro instead in glue?
            if (timeout != K_FOREVER) {
                timeout = OS_TICKS_PER_SEC * timeout / 1000;
            }

            ev = os_eventq_poll(&eventq_pool, 1, timeout);
            bt_mesh_proxy_adv_stop();
        }
#else
        ev = os_eventq_get(&adv_queue);
#endif

        if (!ev || !ev->ev_arg) {
            continue;
        }

        adv_data = ev->ev_arg;
        adv = BT_MESH_ADV(adv_data);

        /* busy == 0 means this was canceled */
        if (adv->busy) {
            adv_send(adv_data);
        }

        os_sched(NULL);
    }
}

void bt_mesh_adv_update(void)
{
    static struct os_event ev = { };

	BT_DBG("");

	os_eventq_put(&adv_queue, &ev);
}

struct os_mbuf *bt_mesh_adv_create(enum bt_mesh_adv_type type, u8_t xmit_count,
				   u8_t xmit_int, s32_t timeout)
{
    struct os_mbuf *adv_data;
    struct bt_mesh_adv *adv;

    adv_data = os_mbuf_get_pkthdr(&adv_os_mbuf_pool, sizeof(struct bt_mesh_adv));
    if (!adv_data) {
        return NULL;
    }

    adv = BT_MESH_ADV(adv_data);
    memset(adv, 0, sizeof(*adv));

    adv->type = type;
    adv->count = xmit_count;
    adv->adv_int = xmit_int;
    adv->ref_cnt = 1;
    adv->ev.ev_arg = adv_data;
    return adv_data;
}

void bt_mesh_adv_send(struct os_mbuf *buf, bt_mesh_adv_func_t sent)
{
	BT_DBG("buf %p, type 0x%02x len %u: %s", buf, BT_MESH_ADV(buf)->type, buf->om_len,
	       bt_hex(buf->om_data, buf->om_len));

	BT_MESH_ADV(buf)->sent = sent;
	BT_MESH_ADV(buf)->busy = 1;
	BT_MESH_ADV(buf)->ev.ev_cb = NULL; /* does not matter */

	net_buf_put(&adv_queue, net_buf_ref(buf));
}

static void bt_mesh_scan_cb(const bt_addr_le_t *addr, s8_t rssi,
                            u8_t adv_type, struct os_mbuf *buf)
{
	if (adv_type != BLE_HCI_ADV_TYPE_ADV_NONCONN_IND) {
		return;
	}

#if BT_MESH_EXTENDED_DEBUG
	BT_DBG("len %u: %s", buf->om_len, bt_hex(buf->om_data, buf->om_len));
#endif

	while (buf->om_len > 1) {
		struct net_buf_simple_state state;
		u8_t len, type;

		len = net_buf_simple_pull_u8(buf);
		/* Check for early termination */
		if (len == 0) {
			return;
		}

		if (len > buf->om_len || buf->om_len < 1) {
			BT_WARN("AD malformed");
			return;
		}

		net_buf_simple_save(buf, &state);

		type = net_buf_simple_pull_u8(buf);

		switch (type) {
		case BLE_HS_ADV_TYPE_MESH_MESSAGE:
			bt_mesh_net_recv(buf, rssi, BT_MESH_NET_IF_ADV);
			break;
#if MYNEWT_VAL(BLE_MESH_PB_ADV)
		case BLE_HS_ADV_TYPE_MESH_PROV:
			bt_mesh_pb_adv_recv(buf);
			break;
#endif
		case BLE_HS_ADV_TYPE_MESH_BEACON:
			bt_mesh_beacon_recv(buf);
			break;
		default:
			break;
		}

		net_buf_simple_restore(buf, &state);
		net_buf_simple_pull(buf, len);
	}
}

void bt_mesh_adv_init(void)
{
    os_stack_t *pstack;
    int rc;

    pstack = malloc(sizeof(os_stack_t) * ADV_STACK_SIZE);
    assert(pstack);

    rc = os_mempool_init(&adv_buf_mempool, MYNEWT_VAL(BLE_MESH_ADV_BUF_COUNT),
    BT_MESH_ADV_DATA_SIZE + BT_MESH_ADV_USER_DATA_SIZE,
                         adv_buf_mem, "adv_buf_pool");
    assert(rc == 0);

    rc = os_mbuf_pool_init(&adv_os_mbuf_pool, &adv_buf_mempool,
                           BT_MESH_ADV_DATA_SIZE + BT_MESH_ADV_USER_DATA_SIZE,
                           MYNEWT_VAL(BLE_MESH_ADV_BUF_COUNT));
    assert(rc == 0);

    os_eventq_init(&adv_queue);

    os_task_init(&adv_task, "mesh_adv", adv_thread, NULL,
                 MYNEWT_VAL(BLE_MESH_ADV_TASK_PRIO), OS_WAIT_FOREVER, pstack,
                 ADV_STACK_SIZE);
}

int
ble_adv_gap_mesh_cb(struct ble_gap_event *event, void *arg)
{
#if MYNEWT_VAL(BLE_EXT_ADV)
    struct ble_gap_ext_disc_desc *ext_desc;
#endif
    struct ble_gap_disc_desc *desc;
    struct os_mbuf *buf = NULL;

#if BT_MESH_EXTENDED_DEBUG
    BT_DBG("event->type %d", event->type);
#endif

    switch (event->type) {
#if MYNEWT_VAL(BLE_EXT_ADV)
        case BLE_GAP_EVENT_EXT_DISC:
            ext_desc = &event->ext_disc;
            buf = os_mbuf_get_pkthdr(&adv_os_mbuf_pool, 0);
            if (!buf || os_mbuf_append(buf, ext_desc->data, ext_desc->length_data)) {
                BT_ERR("Could not append data");
                goto done;
            }
            bt_mesh_scan_cb(&ext_desc->addr, ext_desc->rssi,
                            ext_desc->legacy_event_type, buf);
            break;
#endif
        case BLE_GAP_EVENT_DISC:
            desc = &event->disc;
            buf = os_mbuf_get_pkthdr(&adv_os_mbuf_pool, 0);
            if (!buf || os_mbuf_append(buf, desc->data, desc->length_data)) {
                BT_ERR("Could not append data");
                goto done;
            }

            bt_mesh_scan_cb(&desc->addr, desc->rssi, desc->event_type, buf);
            break;
        default:
            break;
    }

done:
    if (buf) {
        os_mbuf_free_chain(buf);
    }

    return 0;
}

int bt_mesh_scan_enable(void)
{
    struct ble_gap_disc_params scan_param =
        { .passive = 1, .filter_duplicates = 0, .itvl =
        MESH_SCAN_INTERVAL, .window = MESH_SCAN_WINDOW };

    BT_DBG("");

    return ble_gap_disc(g_mesh_addr_type, BLE_HS_FOREVER, &scan_param, NULL, NULL);
}

int bt_mesh_scan_disable(void)
{
	BT_DBG("");

	return ble_gap_disc_cancel();
}
