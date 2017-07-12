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

#ifndef H_BLE_GATT_
#define H_BLE_GATT_

#include <inttypes.h>
#include "host/ble_att.h"
#include "host/ble_uuid.h"
#ifdef __cplusplus
extern "C" {
#endif

struct ble_hs_conn;
struct ble_att_error_rsp;
struct ble_hs_cfg;

#define BLE_GATT_REGISTER_OP_SVC                        1
#define BLE_GATT_REGISTER_OP_CHR                        2
#define BLE_GATT_REGISTER_OP_DSC                        3

#define BLE_GATT_SVC_UUID16                             0x1801
#define BLE_GATT_DSC_CLT_CFG_UUID16                     0x2902

#define BLE_GATT_CHR_PROP_BROADCAST                     0x01
#define BLE_GATT_CHR_PROP_READ                          0x02
#define BLE_GATT_CHR_PROP_WRITE_NO_RSP                  0x04
#define BLE_GATT_CHR_PROP_WRITE                         0x08
#define BLE_GATT_CHR_PROP_NOTIFY                        0x10
#define BLE_GATT_CHR_PROP_INDICATE                      0x20
#define BLE_GATT_CHR_PROP_AUTH_SIGN_WRITE               0x40
#define BLE_GATT_CHR_PROP_EXTENDED                      0x80

#define BLE_GATT_ACCESS_OP_READ_CHR                     0
#define BLE_GATT_ACCESS_OP_WRITE_CHR                    1
#define BLE_GATT_ACCESS_OP_READ_DSC                     2
#define BLE_GATT_ACCESS_OP_WRITE_DSC                    3

#define BLE_GATT_CHR_F_BROADCAST                        0x0001
#define BLE_GATT_CHR_F_READ                             0x0002
#define BLE_GATT_CHR_F_WRITE_NO_RSP                     0x0004
#define BLE_GATT_CHR_F_WRITE                            0x0008
#define BLE_GATT_CHR_F_NOTIFY                           0x0010
#define BLE_GATT_CHR_F_INDICATE                         0x0020
#define BLE_GATT_CHR_F_AUTH_SIGN_WRITE                  0x0040
#define BLE_GATT_CHR_F_RELIABLE_WRITE                   0x0080
#define BLE_GATT_CHR_F_AUX_WRITE                        0x0100
#define BLE_GATT_CHR_F_READ_ENC                         0x0200
#define BLE_GATT_CHR_F_READ_AUTHEN                      0x0400
#define BLE_GATT_CHR_F_READ_AUTHOR                      0x0800
#define BLE_GATT_CHR_F_WRITE_ENC                        0x1000
#define BLE_GATT_CHR_F_WRITE_AUTHEN                     0x2000
#define BLE_GATT_CHR_F_WRITE_AUTHOR                     0x4000

#define BLE_GATT_SVC_TYPE_END                           0
#define BLE_GATT_SVC_TYPE_PRIMARY                       1
#define BLE_GATT_SVC_TYPE_SECONDARY                     2

/*** @client. */
struct ble_gatt_error {
    uint16_t status;
    uint16_t att_handle;
};

struct ble_gatt_svc {
    uint16_t start_handle;
    uint16_t end_handle;
    ble_uuid_any_t uuid;
};

struct ble_gatt_attr {
    uint16_t handle;
    uint16_t offset;
    struct os_mbuf *om;
};

struct ble_gatt_chr {
    uint16_t def_handle;
    uint16_t val_handle;
    uint8_t properties;
    ble_uuid_any_t uuid;
};

struct ble_gatt_dsc {
    uint16_t handle;
    ble_uuid_any_t uuid;
};

typedef int ble_gatt_mtu_fn(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            uint16_t mtu, void *arg);
typedef int ble_gatt_disc_svc_fn(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 const struct ble_gatt_svc *service,
                                 void *arg);

/**
 * The host will free the attribute mbuf automatically after the callback is
 * executed.  The application can take ownership of the mbuf and prevent it
 * from being freed by assigning NULL to attr->om.
 */
typedef int ble_gatt_attr_fn(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             struct ble_gatt_attr *attr,
                             void *arg);

/**
 * The host will free the attribute mbufs automatically after the callback is
 * executed.  The application can take ownership of the mbufs and prevent them
 * from being freed by assigning NULL to each attribute's om field.
 */
typedef int ble_gatt_reliable_attr_fn(uint16_t conn_handle,
                                      const struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attrs,
                                      uint8_t num_attrs, void *arg);

typedef int ble_gatt_chr_fn(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            const struct ble_gatt_chr *chr, void *arg);

typedef int ble_gatt_dsc_fn(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            uint16_t chr_def_handle,
                            const struct ble_gatt_dsc *dsc,
                            void *arg);

int ble_gattc_exchange_mtu(uint16_t conn_handle,
                           ble_gatt_mtu_fn *cb, void *cb_arg);
int ble_gattc_disc_all_svcs(uint16_t conn_handle,
                            ble_gatt_disc_svc_fn *cb, void *cb_arg);
int ble_gattc_disc_svc_by_uuid(uint16_t conn_handle, const ble_uuid_t *uuid,
                               ble_gatt_disc_svc_fn *cb, void *cb_arg);
int ble_gattc_find_inc_svcs(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle,
                            ble_gatt_disc_svc_fn *cb, void *cb_arg);
int ble_gattc_disc_all_chrs(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle, ble_gatt_chr_fn *cb,
                            void *cb_arg);
int ble_gattc_disc_chrs_by_uuid(uint16_t conn_handle, uint16_t start_handle,
                               uint16_t end_handle, const ble_uuid_t *uuid,
                               ble_gatt_chr_fn *cb, void *cb_arg);
int ble_gattc_disc_all_dscs(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle,
                            ble_gatt_dsc_fn *cb, void *cb_arg);
int ble_gattc_read(uint16_t conn_handle, uint16_t attr_handle,
                   ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_read_by_uuid(uint16_t conn_handle, uint16_t start_handle,
                           uint16_t end_handle, const ble_uuid_t *uuid,
                           ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_read_long(uint16_t conn_handle, uint16_t handle, uint16_t offset,
                        ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_read_mult(uint16_t conn_handle, const uint16_t *handles,
                        uint8_t num_handles, ble_gatt_attr_fn *cb,
                        void *cb_arg);
int ble_gattc_write_no_rsp(uint16_t conn_handle, uint16_t attr_handle,
                           struct os_mbuf *om);
int ble_gattc_write_no_rsp_flat(uint16_t conn_handle, uint16_t attr_handle,
                                const void *data, uint16_t data_len);
int ble_gattc_write(uint16_t conn_handle, uint16_t attr_handle,
                    struct os_mbuf *om,
                    ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_write_flat(uint16_t conn_handle, uint16_t attr_handle,
                         const void *data, uint16_t data_len,
                         ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_write_long(uint16_t conn_handle, uint16_t attr_handle,
                         uint16_t offset, struct os_mbuf *om,
                         ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_write_reliable(uint16_t conn_handle,
                             struct ble_gatt_attr *attrs,
                             int num_attrs, ble_gatt_reliable_attr_fn *cb,
                             void *cb_arg);
int ble_gattc_notify_custom(uint16_t conn_handle, uint16_t att_handle,
                            struct os_mbuf *om);
int ble_gattc_notify(uint16_t conn_handle, uint16_t chr_val_handle);
int ble_gattc_indicate_custom(uint16_t conn_handle, uint16_t chr_val_handle,
                              struct os_mbuf *txom);
int ble_gattc_indicate(uint16_t conn_handle, uint16_t chr_val_handle);

int ble_gattc_init(void);

/*** @server. */

struct ble_gatt_access_ctxt;
typedef int ble_gatt_access_fn(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

typedef uint16_t ble_gatt_chr_flags;

struct ble_gatt_chr_def {
    /**
     * Pointer to characteristic UUID; use BLE_UUIDxx_DECLARE macros to declare
     * proper UUID; NULL if there are no more characteristics in the service.
     */
    const ble_uuid_t *uuid;

    /**
     * Callback that gets executed when this characteristic is read or
     * written.
     */
    ble_gatt_access_fn *access_cb;

    /** Optional argument for callback. */
    void *arg;

    /**
     * Array of this characteristic's descriptors.  NULL if no descriptors.
     * Do not include CCCD; it gets added automatically if this
     * characteristic's notify or indicate flag is set.
     */
    struct ble_gatt_dsc_def *descriptors;

    /** Specifies the set of permitted operations for this characteristic. */
    ble_gatt_chr_flags flags;

    /** Specifies minimum required key size to access this characteristic. */
    uint8_t min_key_size;

    /** 
     * At registration time, this is filled in with the characteristic's value
     * attribute handle.
     */
    uint16_t * const val_handle;
};

struct ble_gatt_svc_def {
    /**
     * One of the following:
     *     o BLE_GATT_SVC_TYPE_PRIMARY - primary service
     *     o BLE_GATT_SVC_TYPE_SECONDARY - secondary service
     *     o 0 - No more services in this array.
     */
    uint8_t type;

    /**
     * Pointer to service UUID; use BLE_UUIDxx_DECLARE macros to declare
     * proper UUID; NULL if there are no more characteristics in the service.
     */
    const ble_uuid_t *uuid;

    /**
     * Array of pointers to other service definitions.  These services are
     * reported as "included services" during service discovery.  Terminate the
     * array with NULL.
     */
    const struct ble_gatt_svc_def **includes;

    /**
     * Array of characteristic definitions corresponding to characteristics
     * belonging to this service.
     */
    const struct ble_gatt_chr_def *characteristics;
};

struct ble_gatt_dsc_def {
    /**
     * Pointer to descriptor UUID; use BLE_UUIDxx_DECLARE macros to declare
     * proper UUID; NULL if there are no more characteristics in the service.
     */
    const ble_uuid_t *uuid;

    /** Specifies the set of permitted operations for this descriptor. */
    uint8_t att_flags;

    /** Specifies minimum required key size to access this descriptor. */
    uint8_t min_key_size;

    /** Callback that gets executed when the descriptor is read or written. */
    ble_gatt_access_fn *access_cb;

    /** Optional argument for callback. */
    void *arg;
};

/**
 * Context for an access to a GATT characteristic or descriptor.  When a client
 * reads or writes a locally registered characteristic or descriptor, an
 * instance of this struct gets passed to the application callback.
 */
struct ble_gatt_access_ctxt {
    /**
     * Indicates the gatt operation being performed.  This is equal to one of
     * the following values:
     *     o  BLE_GATT_ACCESS_OP_READ_CHR
     *     o  BLE_GATT_ACCESS_OP_WRITE_CHR
     *     o  BLE_GATT_ACCESS_OP_READ_DSC
     *     o  BLE_GATT_ACCESS_OP_WRITE_DSC
     */
    uint8_t op;

    /**
     * A container for the GATT access data.
     *     o For reads: The application populates this with the value of the
     *       characteristic or descriptor being read.
     *     o For writes: This is already populated with the value being written
     *       by the peer.  If the application wishes to retain this mbuf for
     *       later use, the access callback must set this pointer to NULL to
     *       prevent the stack from freeing it.
     */
    struct os_mbuf *om;

    /**
     * The GATT operation being performed dictates which field in this union is
     * valid.  If a characteristic is being accessed, the chr field is valid.
     * Otherwise a descriptor is being accessed, in which case the dsc field
     * is valid.
     */
    union {
        /**
         * The characteristic definition corresponding to the characteristic
         * being accessed.  This is what the app registered at startup.
         */
        const struct ble_gatt_chr_def *chr;

        /**
         * The descriptor definition corresponding to the descriptor being
         * accessed.  This is what the app registered at startup.
         */
        const struct ble_gatt_dsc_def *dsc;
    };
};

/**
 * Context passed to the registration callback; represents the GATT service,
 * characteristic, or descriptor being registered.
 */
struct ble_gatt_register_ctxt {
    /**
     * Indicates the gatt registration operation just performed.  This is
     * equal to one of the following values:
     *     o BLE_GATT_REGISTER_OP_SVC
     *     o BLE_GATT_REGISTER_OP_CHR
     *     o BLE_GATT_REGISTER_OP_DSC
     */
    uint8_t op;

    /**
     * The value of the op field determines which field in this union is valid.
     */
    union {
        /** Service; valid if op == BLE_GATT_REGISTER_OP_SVC. */
        struct {
            /** The ATT handle of the service definition attribute. */
            uint16_t handle;

            /**
             * The service definition representing the service being
             * registered.
             */
            const struct ble_gatt_svc_def *svc_def;
        } svc;

        /** Characteristic; valid if op == BLE_GATT_REGISTER_OP_CHR. */
        struct {
            /** The ATT handle of the characteristic definition attribute. */
            uint16_t def_handle;

            /** The ATT handle of the characteristic value attribute. */
            uint16_t val_handle;

            /**
             * The characteristic definition representing the characteristic
             * being registered.
             */
            const struct ble_gatt_chr_def *chr_def;

            /**
             * The service definition corresponding to the characteristic's
             * parent service.
             */
            const struct ble_gatt_svc_def *svc_def;
        } chr;

        /** Descriptor; valid if op == BLE_GATT_REGISTER_OP_DSC. */
        struct {
            /** The ATT handle of the descriptor definition attribute. */
            uint16_t handle;

            /**
             * The descriptor definition corresponding to the descriptor being
             * registered.
             */
            const struct ble_gatt_dsc_def *dsc_def;

            /**
             * The characteristic definition corresponding to the descriptor's
             * parent characteristic.
             */
            const struct ble_gatt_chr_def *chr_def;

            /**
             * The service definition corresponding to the descriptor's
             * grandparent service
             */
            const struct ble_gatt_svc_def *svc_def;
        } dsc;
    };
};

typedef void ble_gatt_register_fn(struct ble_gatt_register_ctxt *ctxt,
                                  void *arg);

int ble_gatts_add_svcs(const struct ble_gatt_svc_def *svcs);
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *defs);

void ble_gatts_chr_updated(uint16_t chr_def_handle);

int ble_gatts_find_svc(const ble_uuid_t *uuid, uint16_t *out_handle);
int ble_gatts_find_chr(const ble_uuid_t *svc_uuid, const ble_uuid_t *chr_uuid,
                       uint16_t *out_def_handle, uint16_t *out_val_handle);
int ble_gatts_find_dsc(const ble_uuid_t *svc_uuid, const ble_uuid_t *chr_uuid,
                       const ble_uuid_t *dsc_uuid, uint16_t *out_dsc_handle);

typedef void (*ble_gatt_svc_foreach_fn)(const struct ble_gatt_svc_def *svc,
                                        uint16_t handle,
                                        uint16_t end_group_handle);
void ble_gatts_show_local(void);

#ifdef __cplusplus
}
#endif

#endif
