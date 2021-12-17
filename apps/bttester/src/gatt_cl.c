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

#include <string.h>
#include <errno.h>
#include <assert.h>

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "console/console.h"
#include "services/gatt/ble_svc_gatt.h"
#include "../../../nimble/host/src/ble_att_priv.h"
#include "../../../nimble/host/src/ble_gatt_priv.h"

#include "bttester.h"

#define CONTROLLER_INDEX 0
#define MAX_BUFFER_SIZE 2048

/* Convert UUID from BTP command to bt_uuid */
static uint8_t btp2bt_uuid(const uint8_t *uuid, uint8_t len,
						   ble_uuid_any_t *bt_uuid)
{
	uint16_t le16;

	switch (len) {
		case 0x02: /* UUID 16 */
			bt_uuid->u.type = BLE_UUID_TYPE_16;
			memcpy(&le16, uuid, sizeof(le16));
			BLE_UUID16(bt_uuid)->value = sys_le16_to_cpu(le16);
			break;
		case 0x10: /* UUID 128*/
			bt_uuid->u.type = BLE_UUID_TYPE_128;
			memcpy(BLE_UUID128(bt_uuid)->value, uuid, 16);
			break;
		default:
			return BTP_STATUS_FAILED;
	}
	return BTP_STATUS_SUCCESS;
}

/*
 * gatt_buf - cache used by a gatt client (to cache data read/discovered)
 * and gatt server (to store attribute user_data).
 * It is not intended to be used by client and server at the same time.
 */
static struct {
	uint16_t len;
	uint8_t buf[MAX_BUFFER_SIZE];
	uint16_t cnt;
} gatt_buf;
static struct bt_gatt_subscribe_params {
	uint16_t ccc_handle;
	uint16_t value;
	uint16_t value_handle;
} subscribe_params;

static void *gatt_buf_add(const void *data, size_t len)
{
	void *ptr = gatt_buf.buf + gatt_buf.len;

	if ((len + gatt_buf.len) > MAX_BUFFER_SIZE) {
		return NULL;
	}

	if (data) {
		memcpy(ptr, data, len);
	} else {
		(void) memset(ptr, 0, len);
	}

	gatt_buf.len += len;

	SYS_LOG_DBG("%d/%d used", gatt_buf.len, MAX_BUFFER_SIZE);

	return ptr;
}

static void *gatt_buf_reserve(size_t len)
{
	return gatt_buf_add(NULL, len);
}

static void gatt_buf_clear(void)
{
	(void) memset(&gatt_buf, 0, sizeof(gatt_buf));
}

static void discover_destroy(void)
{
	gatt_buf_clear();
}

static void read_destroy()
{
	gatt_buf_clear();
}

static int tester_mtu_exchanged_ev(uint16_t conn_handle,
								   const struct ble_gatt_error *error,
								   uint16_t mtu, void *arg)
{
	struct gattc_exchange_mtu_ev *ev;
	struct ble_gap_conn_desc conn;
	struct os_mbuf *buf = os_msys_get(0, 0);
	const ble_addr_t *addr;

	SYS_LOG_DBG("");

	if (ble_gap_conn_find(conn_handle, &conn)) {
		goto fail;
	}

	net_buf_simple_init(buf, 0);
	ev = net_buf_simple_add(buf, sizeof(*ev));

	addr = &conn.peer_ota_addr;

	ev->address_type = addr->type;
	memcpy(ev->address, addr->val, sizeof(ev->address));

	ev->mtu = mtu;

	tester_send_buf(BTP_SERVICE_ID_GATTC, GATTC_EV_MTU_EXCHANGED,
					CONTROLLER_INDEX, buf);
fail:
	os_mbuf_free_chain(buf);
	return 0;
}

static void exchange_mtu(uint8_t *data, uint16_t len)
{
	struct ble_gap_conn_desc conn;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	if (ble_gattc_exchange_mtu(conn.conn_handle, tester_mtu_exchanged_ev, NULL)) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_EXCHANGE_MTU,
			   CONTROLLER_INDEX, status);
}

static int disc_prim_svcs_cb(uint16_t conn_handle,
							 const struct ble_gatt_error *error,
							 const struct ble_gatt_svc *gatt_svc, void *arg)
{
	struct gattc_disc_prim_svcs_rp *rp;
	struct ble_gap_conn_desc conn;
	struct gatt_service *service;
	const ble_uuid_any_t *uuid;
	const ble_addr_t *addr;
	uint8_t uuid_length;
	struct os_mbuf *buf = os_msys_get(0, 0);
	uint8_t opcode = (uint8_t) (int) arg;
	uint8_t err = (uint8_t) error->status;
	int rc = 0;

	SYS_LOG_DBG("");

	if (ble_gap_conn_find(conn_handle, &conn)) {
		goto free;
	}

	net_buf_simple_init(buf, 0);
	rp = net_buf_simple_add(buf, sizeof(*rp));

	addr = &conn.peer_ota_addr;

	rp->address_type = addr->type;
	memcpy(rp->address, addr->val, sizeof(rp->address));

	rp->status = err;
	if (error->status != 0 && error->status != BLE_HS_EDONE) {
		rp->services_count = 0;
		tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
						CONTROLLER_INDEX, buf);
		discover_destroy();
		goto free;
	}

	if (error->status == BLE_HS_EDONE) {
		rp->status = 0;
		rp->services_count = gatt_buf.cnt;
		os_mbuf_append(buf, gatt_buf.buf, gatt_buf.len);
		tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
						CONTROLLER_INDEX, buf);
		discover_destroy();
		goto free;
	}

	uuid = &gatt_svc->uuid;
	uuid_length = (uint8_t) (uuid->u.type == BLE_UUID_TYPE_16 ? 2 : 16);

	service = gatt_buf_reserve(sizeof(*service) + uuid_length);
	if (!service) {
		discover_destroy();
		rc = BLE_HS_ENOMEM;
		goto free;
	}

	service->start_handle = sys_cpu_to_le16(gatt_svc->start_handle);
	service->end_handle = sys_cpu_to_le16(gatt_svc->end_handle);
	service->uuid_length = uuid_length;

	if (uuid->u.type == BLE_UUID_TYPE_16) {
		uint16_t u16 = sys_cpu_to_le16(BLE_UUID16(uuid)->value);
		memcpy(service->uuid, &u16, uuid_length);
	} else {
		memcpy(service->uuid, BLE_UUID128(uuid)->value,
			   uuid_length);
	}

	gatt_buf.cnt++;

free:
	os_mbuf_free_chain(buf);
	return rc;
}

static void disc_all_prim_svcs(uint8_t *data, uint16_t len)
{
	struct ble_gap_conn_desc conn;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	if (ble_gattc_disc_all_svcs(conn.conn_handle, disc_prim_svcs_cb,
								(void *) GATTC_DISC_ALL_PRIM_RP)) {
		discover_destroy();
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_DISC_ALL_PRIM_SVCS,
			   CONTROLLER_INDEX, status);
}

static void disc_prim_uuid(uint8_t *data, uint16_t len)
{
	const struct gattc_disc_prim_uuid_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	ble_uuid_any_t uuid;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	if (btp2bt_uuid(cmd->uuid, cmd->uuid_length, &uuid)) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	if (ble_gattc_disc_svc_by_uuid(conn.conn_handle,
								   &uuid.u, disc_prim_svcs_cb,
								   (void *) GATTC_DISC_PRIM_UUID_RP)) {
		discover_destroy();
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_DISC_PRIM_UUID, CONTROLLER_INDEX,
			   status);
}

static int find_included_cb(uint16_t conn_handle,
							const struct ble_gatt_error *error,
							const struct ble_gatt_svc *gatt_svc, void *arg)
{
	struct gattc_find_included_rp *rp;
	struct gatt_included *included;
	const ble_uuid_any_t *uuid;
	int service_handle = (int) arg;
	uint8_t uuid_length;
	uint8_t err = (uint8_t) error->status;
	struct os_mbuf *buf = os_msys_get(0, 0);
	const ble_addr_t *addr;
	struct ble_gap_conn_desc conn;
	int rc = 0;

	SYS_LOG_DBG("");

	if (ble_gap_conn_find(conn_handle, &conn)) {
		rc = BLE_HS_EINVAL;
		goto free;
	}

	net_buf_simple_init(buf, 0);
	rp = net_buf_simple_add(buf, sizeof(*rp));

	addr = &conn.peer_ota_addr;

	rp->address_type = addr->type;
	memcpy(rp->address, addr->val, sizeof(rp->address));

	rp->status = err;

	SYS_LOG_DBG("");
	if (error->status != 0 && error->status != BLE_HS_EDONE) {
		rp->services_count = 0;
		tester_send_buf(BTP_SERVICE_ID_GATTC, GATTC_FIND_INCLUDED_RP,
						CONTROLLER_INDEX, buf);
		discover_destroy();
		goto free;
	}

	if (error->status == BLE_HS_EDONE) {
		rp->status = 0;
		rp->services_count = gatt_buf.cnt;
		os_mbuf_append(buf, gatt_buf.buf, gatt_buf.len);
		tester_send_buf(BTP_SERVICE_ID_GATTC, GATTC_FIND_INCLUDED_RP,
						CONTROLLER_INDEX, buf);
		discover_destroy();
		goto free;
	}

	uuid = &gatt_svc->uuid;
	uuid_length = (uint8_t) (uuid->u.type == BLE_UUID_TYPE_16 ? 2 : 16);

	included = gatt_buf_reserve(sizeof(*included) + uuid_length);
	if (!included) {
		discover_destroy();
		rc = BLE_HS_ENOMEM;
		goto free;
	}

	included->included_handle = sys_cpu_to_le16(service_handle + 1 +
												rp->services_count);
	included->service.start_handle = sys_cpu_to_le16(gatt_svc->start_handle);
	included->service.end_handle = sys_cpu_to_le16(gatt_svc->end_handle);
	included->service.uuid_length = uuid_length;

	if (uuid->u.type == BLE_UUID_TYPE_16) {
		uint16_t u16 = sys_cpu_to_le16(BLE_UUID16(uuid)->value);
		memcpy(included->service.uuid, &u16, uuid_length);
	} else {
		memcpy(included->service.uuid, BLE_UUID128(uuid)->value,
			   uuid_length);
	}

	gatt_buf.cnt++;

free:
	os_mbuf_free_chain(buf);
	return rc;
}

static void find_included(uint8_t *data, uint16_t len)
{
	const struct gattc_find_included_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	uint16_t start_handle, end_handle;
	int service_handle_arg;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	start_handle = sys_le16_to_cpu(cmd->start_handle);
	end_handle = sys_le16_to_cpu(cmd->end_handle);
	service_handle_arg = start_handle;

	if (ble_gattc_find_inc_svcs(conn.conn_handle, start_handle, end_handle,
								find_included_cb,
								(void *) service_handle_arg)) {
		discover_destroy();
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_FIND_INCLUDED, CONTROLLER_INDEX,
			   status);
}

static int disc_chrc_cb(uint16_t conn_handle,
						const struct ble_gatt_error *error,
						const struct ble_gatt_chr *gatt_chr, void *arg)
{
	struct gattc_disc_chrc_rp *rp;
	struct gatt_characteristic *chrc;
	const ble_uuid_any_t *uuid;
	uint8_t uuid_length;
	uint8_t opcode = (uint8_t) (int) arg;
	uint8_t err = (uint8_t) error->status;
	struct os_mbuf *buf = os_msys_get(0, 0);
	const ble_addr_t *addr;
	struct ble_gap_conn_desc conn;
	int rc = 0;

	SYS_LOG_DBG("");
	if (ble_gap_conn_find(conn_handle, &conn)) {
		goto free;
	}

	net_buf_simple_init(buf, 0);
	rp = net_buf_simple_add(buf, sizeof(*rp));

	addr = &conn.peer_ota_addr;

	rp->address_type = addr->type;
	memcpy(rp->address, addr->val, sizeof(rp->address));

	rp->status = err;

	if (ble_gap_conn_find(conn_handle, &conn)) {
		rc = BLE_HS_EINVAL;
		goto free;
	}

	if (error->status != 0 && error->status != BLE_HS_EDONE) {
		rp->characteristics_count = 0;
		tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
						CONTROLLER_INDEX, buf);
		discover_destroy();
		goto free;
	}

	if (error->status == BLE_HS_EDONE) {
		rp->status = 0;
		rp->characteristics_count = gatt_buf.cnt;
		os_mbuf_append(buf, gatt_buf.buf, gatt_buf.len);
		tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
						CONTROLLER_INDEX, buf);
		discover_destroy();
		goto free;
	}

	uuid = &gatt_chr->uuid;
	uuid_length = (uint8_t) (uuid->u.type == BLE_UUID_TYPE_16 ? 2 : 16);

	chrc = gatt_buf_reserve(sizeof(*chrc) + uuid_length);
	if (!chrc) {
		discover_destroy();
		rc = BLE_HS_ENOMEM;
		goto free;
	}

	chrc->characteristic_handle = sys_cpu_to_le16(gatt_chr->def_handle);
	chrc->properties = gatt_chr->properties;
	chrc->value_handle = sys_cpu_to_le16(gatt_chr->val_handle);
	chrc->uuid_length = uuid_length;

	if (uuid->u.type == BLE_UUID_TYPE_16) {
		uint16_t u16 = sys_cpu_to_le16(BLE_UUID16(uuid)->value);
		memcpy(chrc->uuid, &u16, uuid_length);
	} else {
		memcpy(chrc->uuid, BLE_UUID128(uuid)->value,
			   uuid_length);
	}

	gatt_buf.cnt++;
free:
	os_mbuf_free_chain(buf);
	return rc;
}

static void disc_all_chrc(uint8_t *data, uint16_t len)
{
	const struct gattc_disc_all_chrc_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	uint16_t start_handle, end_handle;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		SYS_LOG_DBG("Conn find rsped");
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	start_handle = sys_le16_to_cpu(cmd->start_handle);
	end_handle = sys_le16_to_cpu(cmd->end_handle);

	rc = ble_gattc_disc_all_chrs(conn.conn_handle, start_handle, end_handle,
								 disc_chrc_cb, (void *) GATTC_DISC_ALL_CHRC_RP);
	if (rc) {
		discover_destroy();
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_DISC_ALL_CHRC, CONTROLLER_INDEX,
			   status);
}

static void disc_chrc_uuid(uint8_t *data, uint16_t len)
{
	const struct gattc_disc_chrc_uuid_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	uint16_t start_handle, end_handle;
	ble_uuid_any_t uuid;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	if (btp2bt_uuid(cmd->uuid, cmd->uuid_length, &uuid)) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	start_handle = sys_le16_to_cpu(cmd->start_handle);
	end_handle = sys_le16_to_cpu(cmd->end_handle);

	rc = ble_gattc_disc_chrs_by_uuid(conn.conn_handle, start_handle,
									 end_handle, &uuid.u, disc_chrc_cb,
									 (void *) GATTC_DISC_CHRC_UUID_RP);
	if (rc) {
		discover_destroy();
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_DISC_CHRC_UUID, CONTROLLER_INDEX,
			   status);
}

static int disc_all_desc_cb(uint16_t conn_handle,
							const struct ble_gatt_error *error,
							uint16_t chr_val_handle,
							const struct ble_gatt_dsc *gatt_dsc,
							void *arg)
{
	struct gattc_disc_all_desc_rp *rp;
	struct gatt_descriptor *dsc;
	const ble_uuid_any_t *uuid;
	uint8_t uuid_length;
	uint8_t err = (uint8_t) error->status;
	struct os_mbuf *buf = os_msys_get(0, 0);
	const ble_addr_t *addr;
	struct ble_gap_conn_desc conn;
	int rc = 0;

	SYS_LOG_DBG("");

	if (ble_gap_conn_find(conn_handle, &conn)) {
		rc = BLE_HS_EINVAL;
		goto free;
	}

	net_buf_simple_init(buf, 0);
	rp = net_buf_simple_add(buf, sizeof(*rp));

	addr = &conn.peer_ota_addr;

	rp->address_type = addr->type;
	memcpy(rp->address, addr->val, sizeof(rp->address));

	rp->status = err;

	if (error->status != 0 && error->status != BLE_HS_EDONE) {
		rp->descriptors_count = 0;
		tester_send_buf(BTP_SERVICE_ID_GATTC, GATTC_DISC_ALL_DESC_RP,
						CONTROLLER_INDEX, buf);
		discover_destroy();
		goto free;
	}

	if (error->status == BLE_HS_EDONE) {
		rp->status = 0;
		rp->descriptors_count = gatt_buf.cnt;
		os_mbuf_append(buf, gatt_buf.buf, gatt_buf.len);
		tester_send_buf(BTP_SERVICE_ID_GATTC, GATTC_DISC_ALL_DESC_RP,
						CONTROLLER_INDEX, buf);
		discover_destroy();
		goto free;
	}

	uuid = &gatt_dsc->uuid;
	uuid_length = (uint8_t) (uuid->u.type == BLE_UUID_TYPE_16 ? 2 : 16);

	dsc = gatt_buf_reserve(sizeof(*dsc) + uuid_length);
	if (!dsc) {
		discover_destroy();
		rc = BLE_HS_ENOMEM;
		goto free;
	}

	dsc->descriptor_handle = sys_cpu_to_le16(gatt_dsc->handle);
	dsc->uuid_length = uuid_length;

	if (uuid->u.type == BLE_UUID_TYPE_16) {
		uint16_t u16 = sys_cpu_to_le16(BLE_UUID16(uuid)->value);
		memcpy(dsc->uuid, &u16, uuid_length);
	} else {
		memcpy(dsc->uuid, BLE_UUID128(uuid)->value, uuid_length);
	}

	gatt_buf.cnt++;

free:
	os_mbuf_free_chain(buf);
	return rc;
}

static void disc_all_desc(uint8_t *data, uint16_t len)
{
	const struct gattc_disc_all_desc_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	uint16_t start_handle, end_handle;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	start_handle = sys_le16_to_cpu(cmd->start_handle) - 1;
	end_handle = sys_le16_to_cpu(cmd->end_handle);

	rc = ble_gattc_disc_all_dscs(conn.conn_handle, start_handle, end_handle,
								 disc_all_desc_cb, (void *) GATTC_DISC_ALL_DESC);

	SYS_LOG_DBG("rc=%d", rc);

	if (rc) {
		discover_destroy();
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_DISC_ALL_DESC, CONTROLLER_INDEX,
			   status);
}

static int read_cb(uint16_t conn_handle,
				   const struct ble_gatt_error *error,
				   struct ble_gatt_attr *attr,
				   void *arg)
{
	struct gattc_read_rp *rp;
	uint8_t opcode = (uint8_t) (int) arg;
	struct os_mbuf *buf = os_msys_get(0, 0);
	const ble_addr_t *addr;
	struct ble_gap_conn_desc conn;
	int rc = 0;

	if (ble_gap_conn_find(conn_handle, &conn)) {
		rc = BLE_HS_EINVAL;
		goto free;
	}

	net_buf_simple_init(buf, 0);
	rp = net_buf_simple_add(buf, sizeof(*rp));

	addr = &conn.peer_ota_addr;

	rp->address_type = addr->type;
	memcpy(rp->address, addr->val, sizeof(rp->address));

	SYS_LOG_DBG("status=%d", error->status);

	if (error->status != 0 && error->status != BLE_HS_EDONE) {
		rp->status = (uint8_t) BLE_HS_ATT_ERR(error->status);
		rp->data_length = 0;
		tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
						CONTROLLER_INDEX, buf);
		read_destroy();
		goto free;
	}

	if (!gatt_buf_add(attr->om->om_data, attr->om->om_len)) {
		read_destroy();
		rc = BLE_HS_ENOMEM;
		goto free;
	}

	rp->status = 0;
	rp->data_length = attr->om->om_len;
	os_mbuf_appendfrom(buf, attr->om, 0, os_mbuf_len(attr->om));
	tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
					CONTROLLER_INDEX, buf);
	read_destroy();
free:
	os_mbuf_free_chain(buf);
	return rc;
}

static void read(uint8_t *data, uint16_t len)
{
	const struct gattc_read_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	/* Clear buffer */
	read_destroy();

	if (ble_gattc_read(conn.conn_handle, sys_le16_to_cpu(cmd->handle),
					   read_cb, (void *) GATTC_READ_RP)) {
		read_destroy();
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_READ, CONTROLLER_INDEX,
			   status);
}

static int read_uuid_cb(uint16_t conn_handle,
						const struct ble_gatt_error *error,
						struct ble_gatt_attr *attr,
						void *arg)
{
	struct gattc_read_uuid_rp *rp;
	struct gatt_read_uuid_chr *chr;
	uint8_t opcode = (uint8_t) (int) arg;
	uint8_t err = (uint8_t) error->status;
	struct os_mbuf *buf = os_msys_get(0, 0);
	const ble_addr_t *addr;
	struct ble_gap_conn_desc conn;
	int rc = 0;
	static uint16_t attr_len;

	SYS_LOG_DBG("status=%d", error->status);

	if (ble_gap_conn_find(conn_handle, &conn)) {
		rc = BLE_HS_EINVAL;
		goto free;
	}

	net_buf_simple_init(buf, 0);
	rp = net_buf_simple_add(buf, sizeof(*rp));

	addr = &conn.peer_ota_addr;

	rp->address_type = addr->type;
	memcpy(rp->address, addr->val, sizeof(rp->address));

	rp->status = err;

	if (error->status != 0 && error->status != BLE_HS_EDONE) {
		rp->data_length = 0;
		tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
						CONTROLLER_INDEX, buf);
		read_destroy();
		goto free;
	}

	if (error->status == BLE_HS_EDONE) {
	    rp->data_length = gatt_buf.len;
	    rp->value_length = attr_len;
	    rp->status = 0;
	    os_mbuf_append(buf, gatt_buf.buf+1, gatt_buf.len-1);
	    tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
                        CONTROLLER_INDEX, buf);
	    read_destroy();
	    goto free;
	}

	if (error->status == 0) {
	    attr_len = attr->om->om_len;
	}

	if (gatt_buf_add(attr->om->om_data, attr->om->om_len) == NULL) {
		read_destroy();
		rc = BLE_HS_ENOMEM;
		goto free;
	}

	chr = gatt_buf_reserve(sizeof(*chr) + attr->om->om_len);
	if (!chr) {
		read_destroy();
		rc = BLE_HS_ENOMEM;
		goto free;
	}

	chr->handle = sys_cpu_to_be16(attr->handle);
	memcpy(chr->data, attr->om->om_data, attr->om->om_len);

free:
	os_mbuf_free_chain(buf);
	return rc;
}

static void read_uuid(uint8_t *data, uint16_t len)
{
	const struct gattc_read_uuid_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	ble_uuid_any_t uuid;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	if (btp2bt_uuid(cmd->uuid, cmd->uuid_length, &uuid)) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	/* Clear buffer */
	read_destroy();

	if (ble_gattc_read_by_uuid(conn.conn_handle,
							   sys_le16_to_cpu(cmd->start_handle),
							   sys_le16_to_cpu(cmd->end_handle), &uuid.u,
							   read_uuid_cb, (void *) GATTC_READ_UUID_RP)) {
		read_destroy();
		status = BTP_STATUS_FAILED;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_READ_UUID, CONTROLLER_INDEX,
			   status);
}

static int read_long_cb(uint16_t conn_handle,
						const struct ble_gatt_error *error,
						struct ble_gatt_attr *attr,
						void *arg)
{
	struct gattc_read_rp *rp;;
	uint8_t opcode = (uint8_t) (int) arg;
	uint8_t err = (uint8_t) error->status;
	struct os_mbuf *buf = os_msys_get(0, 0);
	const ble_addr_t *addr;
	struct ble_gap_conn_desc conn;
	int rc = 0;

	SYS_LOG_DBG("status=%d", error->status);

	if (ble_gap_conn_find(conn_handle, &conn)) {
		rc = BLE_HS_EINVAL;
		goto free;
	}

	net_buf_simple_init(buf, 0);
	rp = net_buf_simple_add(buf, sizeof(*rp));

	addr = &conn.peer_ota_addr;

	rp->address_type = addr->type;
	memcpy(rp->address, addr->val, sizeof(rp->address));

	rp->status = err;

	if (error->status != 0 && error->status != BLE_HS_EDONE) {
		rp->data_length = 0;
		tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
						CONTROLLER_INDEX, buf);
		read_destroy();
		goto free;
	}

	if (error->status == BLE_HS_EDONE) {
		rp->status = 0;
		rp->data_length = gatt_buf.len;
		os_mbuf_append(buf, gatt_buf.buf, gatt_buf.len);
		tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
						CONTROLLER_INDEX, buf);
		read_destroy();
		goto free;
	}

	if (gatt_buf_add(attr->om->om_data, attr->om->om_len) == NULL) {
		read_destroy();
		rc = BLE_HS_ENOMEM;
		goto free;
	}

	rp->data_length += attr->om->om_len;

free:
	os_mbuf_free_chain(buf);
	return rc;
}

static void read_long(uint8_t *data, uint16_t len)
{
	const struct gattc_read_long_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	/* Clear buffer */
	read_destroy();

	if (ble_gattc_read_long(conn.conn_handle,
							sys_le16_to_cpu(cmd->handle),
							sys_le16_to_cpu(cmd->offset),
							read_long_cb, (void *) GATTC_READ_LONG_RP)) {
		read_destroy();
		status = BTP_STATUS_FAILED;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_READ_LONG, CONTROLLER_INDEX,
			   status);
}

static void read_multiple(uint8_t *data, uint16_t len)
{
	const struct gattc_read_multiple_cmd *cmd = (void *) data;
	uint16_t handles[cmd->handles_count];
	struct ble_gap_conn_desc conn;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc, i;

	SYS_LOG_DBG("");

	for (i = 0; i < ARRAY_SIZE(handles); i++) {
		handles[i] = sys_le16_to_cpu(cmd->handles[i]);
	}

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	/* Clear buffer */
	read_destroy();

	if (ble_gattc_read_mult(conn.conn_handle, handles,
							cmd->handles_count, read_cb, (void *) GATTC_READ_MULTIPLE_RP)) {
		read_destroy();
		status = BTP_STATUS_FAILED;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_READ_MULTIPLE, CONTROLLER_INDEX,
			   status);
}

static void write_without_rsp(uint8_t *data, uint16_t len, uint8_t op, bool sign)
{
	const struct gattc_write_without_rsp_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	if (ble_gattc_write_no_rsp_flat(conn.conn_handle,
									sys_le16_to_cpu(cmd->handle), cmd->data,
									sys_le16_to_cpu(cmd->data_length))) {
		status = BTP_STATUS_FAILED;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, op, CONTROLLER_INDEX, status);
}

static int write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
					struct ble_gatt_attr *attr,
					void *arg)
{
	struct gattc_write_rp *rp;
	uint8_t err = (uint8_t) error->status;
	uint8_t opcode = (uint8_t) (int) arg;
	struct os_mbuf *buf = os_msys_get(0, 0);
	const ble_addr_t *addr;
	struct ble_gap_conn_desc conn;
	int rc = 0;

	SYS_LOG_DBG("");

	if (ble_gap_conn_find(conn_handle, &conn)) {
		rc = BLE_HS_EINVAL;
		goto free;
	}

	net_buf_simple_init(buf, 0);
	rp = net_buf_simple_add(buf, sizeof(*rp));

	addr = &conn.peer_ota_addr;

	rp->address_type = addr->type;
	memcpy(rp->address, addr->val, sizeof(rp->address));

	rp->status = err;
	tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
					CONTROLLER_INDEX, buf);
free:
	os_mbuf_free_chain(buf);
	return rc;
}

static void write(uint8_t *data, uint16_t len)
{
	const struct gattc_write_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	if (ble_gattc_write_flat(conn.conn_handle, sys_le16_to_cpu(cmd->handle),
							 cmd->data, sys_le16_to_cpu(cmd->data_length),
							 write_cb, (void *) GATTC_WRITE_RP)) {
		status = BTP_STATUS_FAILED;
	}

rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_WRITE, CONTROLLER_INDEX,
			   status);
}

static void write_long(uint8_t *data, uint16_t len)
{
	const struct gattc_write_long_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	struct os_mbuf *om = NULL;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc = 0;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		goto fail;
	}

	om = ble_hs_mbuf_from_flat(cmd->data, sys_le16_to_cpu(cmd->data_length));
	if (!om) {
		SYS_LOG_ERR("Insufficient resources");
		status = BTP_STATUS_FAILED;
		goto fail;
	}

	rc = ble_gattc_write_long(conn.conn_handle,
							  sys_le16_to_cpu(cmd->handle),
							  sys_le16_to_cpu(cmd->offset),
							  om, write_cb,
							  (void *) GATTC_WRITE_LONG_RP);
	if (rc) {
		status = BTP_STATUS_FAILED;
	} else {
		goto rsp;
	}

fail:
	SYS_LOG_ERR("Failed to send Write Long request, rc=%d", rc);
	os_mbuf_free_chain(om);
rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_WRITE_LONG, CONTROLLER_INDEX,
			   status);
}

static int reliable_write_cb(uint16_t conn_handle,
							 const struct ble_gatt_error *error,
							 struct ble_gatt_attr *attrs,
							 uint8_t num_attrs,
							 void *arg)
{
	struct gattc_write_rp *rp;
	uint8_t err = (uint8_t) error->status;
	struct os_mbuf *buf = os_msys_get(0, 0);
	const ble_addr_t *addr;
	struct ble_gap_conn_desc conn;
	int rc = 0;

	SYS_LOG_DBG("");

	if (ble_gap_conn_find(conn_handle, &conn)) {
		rc = BLE_HS_EINVAL;
		goto free;
	}

	net_buf_simple_init(buf, 0);
	rp = net_buf_simple_add(buf, sizeof(*rp));

	addr = &conn.peer_ota_addr;

	rp->address_type = addr->type;
	memcpy(rp->address, addr->val, sizeof(rp->address));

	rp->status = err;
	tester_send_buf(BTP_SERVICE_ID_GATTC, GATTC_RELIABLE_WRITE_RP,
					CONTROLLER_INDEX, buf);
free:
	os_mbuf_free_chain(buf);
	return rc;
}

static void reliable_write(uint8_t *data, uint16_t len)
{
	const struct gattc_reliable_write_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	struct ble_gatt_attr attr;
	struct os_mbuf *om = NULL;
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto fail;
	}

	om = ble_hs_mbuf_from_flat(cmd->data, sys_le16_to_cpu(cmd->data_length));
	/* This is required, because Nimble checks if
	* the data is longer than offset
	*/
	if (os_mbuf_extend(om, sys_le16_to_cpu(cmd->offset) + 1) == NULL) {
		status = BTP_STATUS_FAILED;
		goto fail;
	}

	attr.handle = sys_le16_to_cpu(cmd->handle);
	attr.offset = sys_le16_to_cpu(cmd->offset);
	attr.om = om;

	if (ble_gattc_write_reliable(conn.conn_handle, &attr, 1,
								 reliable_write_cb, NULL)) {
		status = BTP_STATUS_FAILED;
		goto fail;
	} else {
		goto rsp;
	}

fail:
	os_mbuf_free_chain(om);
rsp:
	tester_rsp(BTP_SERVICE_ID_GATTC, GATTC_WRITE_LONG, CONTROLLER_INDEX,
			   status);
}

static int subscribe_cb(uint16_t conn_handle,
						const struct ble_gatt_error *error,
						struct ble_gatt_attr *attrs,
						void *arg)
{
	struct subscribe_rp *rp;
	uint8_t err = (uint8_t) error->status;
	uint8_t opcode = (uint8_t) (int) arg;
	struct os_mbuf *buf = os_msys_get(0, 0);
	const ble_addr_t *addr;
	struct ble_gap_conn_desc conn;
	int rc = 0;

	SYS_LOG_DBG("");

	if (ble_gap_conn_find(conn_handle, &conn)) {
		rc = BLE_HS_EINVAL;
		goto free;
	}

	net_buf_simple_init(buf, 0);
	rp = net_buf_simple_add(buf, sizeof(*rp));

	addr = &conn.peer_ota_addr;

	rp->address_type = addr->type;
	memcpy(rp->address, addr->val, sizeof(rp->address));

	rp->status = err;
	tester_send_buf(BTP_SERVICE_ID_GATTC, opcode,
					CONTROLLER_INDEX, buf);
free:
	os_mbuf_free_chain(buf);
	return rc;
}

static int enable_subscription(uint16_t conn_handle, uint16_t ccc_handle,
							   uint16_t value)
{
	uint32_t opcode;

	SYS_LOG_DBG("");

	opcode = (uint32_t) (value == 0x0001 ? GATTC_CFG_NOTIFY_RP : GATTC_CFG_INDICATE_RP);

	if (ble_gattc_write_flat(conn_handle, ccc_handle,
							 &value, sizeof(value), subscribe_cb, (void *) opcode)) {
		return -EINVAL;
	}

	subscribe_params.ccc_handle = value;

	return 0;
}

static int disable_subscription(uint16_t conn_handle, uint16_t ccc_handle)
{
	uint16_t value = 0x00;
	uint32_t opcode;

	SYS_LOG_DBG("");

	opcode = (uint32_t) (value == 0x0001 ? GATTC_CFG_NOTIFY_RP : GATTC_CFG_INDICATE_RP);

	/* Fail if CCC handle doesn't match */
	if (ccc_handle != subscribe_params.ccc_handle) {
		SYS_LOG_ERR("CCC handle doesn't match");
		return -EINVAL;
	}

	if (ble_gattc_write_flat(conn_handle, ccc_handle,
							 &value, sizeof(value), subscribe_cb, (void *) opcode)) {
		return -EINVAL;
	}

	subscribe_params.ccc_handle = 0;
	return 0;
}

static void config_subscription(uint8_t *data, uint16_t len, uint8_t op)
{
	const struct gattc_cfg_notify_cmd *cmd = (void *) data;
	struct ble_gap_conn_desc conn;
	uint16_t ccc_handle = sys_le16_to_cpu(cmd->ccc_handle);
	uint8_t status = BTP_STATUS_SUCCESS;
	int rc;

	SYS_LOG_DBG("");

	rc = ble_gap_conn_find_by_addr((ble_addr_t *) data, &conn);
	if (rc) {
		status = BTP_STATUS_FAILED;
		goto rsp;
	}

	if (cmd->enable) {
		uint16_t value;

		if (op == GATTC_CFG_NOTIFY) {
			value = 0x0001;
		} else {
			value = 0x0002;
		}

		if (enable_subscription(conn.conn_handle,
								ccc_handle, value) != 0) {
			status = BTP_STATUS_FAILED;
			goto rsp;
		}
	} else {
		if (disable_subscription(conn.conn_handle, ccc_handle) < 0) {
			status = BTP_STATUS_FAILED;
		} else {
			status = BTP_STATUS_SUCCESS;
		}
	}

rsp:
	SYS_LOG_DBG("Config subscription (op %u) status %u", op, status);

	tester_rsp(BTP_SERVICE_ID_GATTC, op, CONTROLLER_INDEX, status);
}

int tester_gattc_notify_rx_ev(uint16_t conn_handle, uint16_t attr_handle,
							  uint8_t indication, struct os_mbuf *om)
{
	struct gattc_notification_ev *ev;
	struct ble_gap_conn_desc conn;
	struct os_mbuf *buf = os_msys_get(0, 0);
	const ble_addr_t *addr;

	SYS_LOG_DBG("");

	if (!subscribe_params.ccc_handle) {
		goto fail;
	}

	if (ble_gap_conn_find(conn_handle, &conn)) {
		goto fail;
	}

	net_buf_simple_init(buf, 0);
	ev = net_buf_simple_add(buf, sizeof(*ev));

	addr = &conn.peer_ota_addr;

	ev->address_type = addr->type;
	memcpy(ev->address, addr->val, sizeof(ev->address));
	ev->type = (uint8_t) (indication ? 0x02 : 0x01);
	ev->handle = sys_cpu_to_le16(attr_handle);
	ev->data_length = sys_cpu_to_le16(os_mbuf_len(om));
	os_mbuf_appendfrom(buf, om, 0, os_mbuf_len(om));

	tester_send_buf(BTP_SERVICE_ID_GATTC, GATTC_EV_NOTIFICATION_RXED,
					CONTROLLER_INDEX, buf);

fail:
	os_mbuf_free_chain(buf);
	return 0;
}

static void supported_commands(uint8_t *data, uint16_t len)
{
	uint8_t cmds[3];
	struct gatt_read_supported_commands_rp *rp = (void *) cmds;

	SYS_LOG_DBG("");

	memset(cmds, 0, sizeof(cmds));

	tester_set_bit(cmds, GATTC_READ_SUPPORTED_COMMANDS);
	tester_set_bit(cmds, GATTC_EXCHANGE_MTU);
	tester_set_bit(cmds, GATTC_DISC_ALL_PRIM_SVCS);
	tester_set_bit(cmds, GATTC_DISC_PRIM_UUID);
	tester_set_bit(cmds, GATTC_FIND_INCLUDED);
	tester_set_bit(cmds, GATTC_DISC_ALL_CHRC);
	tester_set_bit(cmds, GATTC_DISC_CHRC_UUID);
	tester_set_bit(cmds, GATTC_DISC_ALL_DESC);
	tester_set_bit(cmds, GATTC_READ);
	tester_set_bit(cmds, GATTC_READ_UUID);
	tester_set_bit(cmds, GATTC_READ_LONG);
	tester_set_bit(cmds, GATTC_READ_MULTIPLE);
	tester_set_bit(cmds, GATTC_WRITE_WITHOUT_RSP);
#if 0
	tester_set_bit(cmds, GATTC_SIGNED_WRITE_WITHOUT_RSP);
#endif
	tester_set_bit(cmds, GATTC_WRITE);
	tester_set_bit(cmds, GATTC_WRITE_LONG);
	tester_set_bit(cmds, GATTC_RELIABLE_WRITE);
	tester_set_bit(cmds, GATTC_CFG_NOTIFY);
	tester_set_bit(cmds, GATTC_CFG_INDICATE);

	tester_send(BTP_SERVICE_ID_GATTC, GATTC_READ_SUPPORTED_COMMANDS,
				CONTROLLER_INDEX, (uint8_t *) rp, sizeof(cmds));
}

void tester_handle_gattc(uint8_t opcode, uint8_t index, uint8_t *data,
						 uint16_t len)
{
	switch (opcode) {
		case GATTC_READ_SUPPORTED_COMMANDS:
			supported_commands(data, len);
			return;
		case GATTC_EXCHANGE_MTU:
			exchange_mtu(data, len);
			return;
		case GATTC_DISC_ALL_PRIM_SVCS:
			disc_all_prim_svcs(data, len);
			return;
		case GATTC_DISC_PRIM_UUID:
			disc_prim_uuid(data, len);
			return;
		case GATTC_FIND_INCLUDED:
			find_included(data, len);
			return;
		case GATTC_DISC_ALL_CHRC:
			disc_all_chrc(data, len);
			return;
		case GATTC_DISC_CHRC_UUID:
			disc_chrc_uuid(data, len);
			return;
		case GATTC_DISC_ALL_DESC:
			disc_all_desc(data, len);
			return;
		case GATTC_READ:
			read(data, len);
			return;
		case GATTC_READ_UUID:
			read_uuid(data, len);
			return;
		case GATTC_READ_LONG:
			read_long(data, len);
			return;
		case GATTC_READ_MULTIPLE:
			read_multiple(data, len);
			return;
		case GATTC_WRITE_WITHOUT_RSP:
			write_without_rsp(data, len, opcode, false);
			return;
#if 0
			case GATTC_SIGNED_WRITE_WITHOUT_RSP:
				write_without_rsp(data, len, opcode, true);
				return;
#endif
		case GATTC_WRITE:
			write(data, len);
			return;
		case GATTC_WRITE_LONG:
			write_long(data, len);
			return;
		case GATTC_RELIABLE_WRITE:
			reliable_write(data, len);
			return;
		case GATTC_CFG_NOTIFY:
		case GATTC_CFG_INDICATE:
			config_subscription(data, len, opcode);
			return;
		default:
			tester_rsp(BTP_SERVICE_ID_GATTC, opcode, index,
					   BTP_STATUS_UNKNOWN_CMD);
			return;
	}
}