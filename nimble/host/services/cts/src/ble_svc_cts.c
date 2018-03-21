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

#include <assert.h>
#include <string.h>

#include "sysinit/sysinit.h"
#include "syscfg/syscfg.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/cts/ble_svc_cts.h"
#include "datetime/datetime.h"

/* To use for days AND hours if update more than 255 days ago */
#define BLE_SVC_CTS_UPDATED_SINCE_A_LONG_TIME_AGO 255

#define BLE_SVC_CTS_ADJUST_REASON_MANUAL_TIME_UPDATE               1
#define BLE_SVC_CTS_ADJUST_REASON_EXTERN_REFERENCE_TIME_UPDATE     2
#define BLE_SVC_CTS_ADJUST_REASON_CHANGE_OF_TIMEZONE               4
#define BLE_SVC_CTS_ADJUST_REASON_CHANGE_OF_DST                    8

static bool     ble_svc_cts_reference_time_was_used  = false;
static uint64_t ble_svc_cts_reference_time_last_used = 0;
static uint8_t  ble_svc_cts_adjust_reason = 0;

/* Used as buffer */
static struct {
    uint8_t time_source;
    uint8_t time_accuracy;
    uint8_t days;
    uint8_t hours;
} __attribute__((__packed__)) ble_svc_cts_reference_time_information = {
    .time_source   = BLE_SVC_CTS_TIME_SOURCE_UNKNOWN,
    .time_accuracy = BLE_SVC_CTS_TIME_ACCURACY_UNKNOWN,
    .days          = BLE_SVC_CTS_UPDATED_SINCE_A_LONG_TIME_AGO,
    .hours         = BLE_SVC_CTS_UPDATED_SINCE_A_LONG_TIME_AGO,
};

static struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hours;
    uint8_t minutes;
    uint8_t secondes;
    uint8_t day_of_week;
    uint8_t fraction256;
    uint8_t adjust_reason;
} __attribute__((__packed__)) ble_svc_cts_current_time;

static struct {
    int8_t  time_zone;  /* -48 .. 56, -128 = unknown */
    uint8_t dst_offset; 
} __attribute__((__packed__)) ble_svc_cts_local_time_information;
    
/* Charachteristic value handles */
#if MYNEWT_VAL(BLE_SVC_CTS_CURRENT_TIME_NOTIFY_ENABLE) > 0
static uint16_t ble_svc_cts_current_time_handle;
#endif

/* Connection handle 
 *
 * TODO: In order to support multiple connections we would need to save
 *       the handles for every connection, not just the most recent. Then
 *       we would need to notify each connection when needed.
 * */
static uint16_t ble_svc_cts_conn_handle;

/* Access function */
static int
ble_svc_cts_access(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def ble_svc_cts_defs[] = {
    {
        /*** Current Time Service. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_CTS_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) { {
	    /*** Current time characteristic */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_CTS_CHR_UUID16_CURRENT_TIME),
            .access_cb = ble_svc_cts_access,
#if MYNEWT_VAL(BLE_SVC_CTS_CURRENT_TIME_NOTIFY_ENABLE) > 0
	    .val_handle = &ble_svc_cts_current_time_handle,
#endif
            .flags = BLE_GATT_CHR_F_READ |
	             MYNEWT_VAL(BLE_SVC_CTS_CURRENT_TIME_READ_PERM) |
#if MYNEWT_VAL(BLE_SVC_CTS_CURRENT_TIME_NOTIFY_ENABLE) > 0
	             BLE_GATT_CHR_F_NOTIFY |
#endif
#if MYNEWT_VAL(BLE_SVC_GAP_DEVICE_NAME_WRITE_PERM) >= 0
                     BLE_GATT_CHR_F_WRITE |
                     MYNEWT_VAL(BLE_SVC_CTS_CURRENT_TIME_WRITE_PERM) |
#endif
	             0,
	    }, {
#if MYNEWT_VAL(BLE_SVC_CTS_LOCAL_TIME_INFORMATION_READ_PERM) >= 0
	    /*** Local time information characteristic */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_CTS_CHR_UUID16_LOCAL_TIME_INFORMATION),
            .access_cb = ble_svc_cts_access,
            .flags = BLE_GATT_CHR_F_READ |
	             MYNEWT_VAL(BLE_SVC_CTS_LOCAL_TIME_INFORMATION_READ_PERM) |
#if MYNEWT_VAL(BLE_SVC_CTS_LOCAL_TIME_INFORMATION_WRITE_PERM) >= 0
                     BLE_GATT_CHR_F_WRITE |
                     MYNEWT_VAL(BLE_SVC_CTS_LOCAL_TIME_INFORMATION_WRITE_PERM) |
#endif
	             0,
	    }, {
#endif
#if MYNEWT_VAL(BLE_SVC_CTS_REFERENCE_TIME_INFORMATION_READ_PERM) >= 0
	    /*** Reference time information characteristic */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_CTS_CHR_UUID16_REFERENCE_TIME_INFORMATION),
            .access_cb = ble_svc_cts_access,
            .flags = BLE_GATT_CHR_F_READ |
	             MYNEWT_VAL(BLE_SVC_CTS_REFERENCE_TIME_INFORMATION_READ_PERM),
	    }, {
#endif
            0, /* No more characteristics in this service. */
        } },
    },

    {
        0, /* No more services. */
    },
};

static inline int
ble_svc_cts_current_time_read_access(struct ble_gatt_access_ctxt *ctxt)
{
    /* Get current clock time */
    struct os_timeval  tv;
    struct os_timezone tz;
    struct clocktime   ct;
    
    if (os_gettimeofday(&tv, &tz) || timeval_to_clocktime(&tv, &tz, &ct)) {
	return BLE_ATT_ERR_UNLIKELY;
    }

    /* Populate data */
    ble_svc_cts_current_time.year          = htole16(ct.year);
    ble_svc_cts_current_time.month         = ct.mon;
    ble_svc_cts_current_time.day           = ct.day;
    ble_svc_cts_current_time.hours         = ct.hour;
    ble_svc_cts_current_time.minutes       = ct.min;
    ble_svc_cts_current_time.secondes      = ct.sec;
    ble_svc_cts_current_time.day_of_week   = (ct.dow == 0) ? 7 : ct.dow;
    ble_svc_cts_current_time.fraction256   = (ct.usec * 256) / 1000000;
    ble_svc_cts_current_time.adjust_reason = ble_svc_cts_adjust_reason;

    /* Copy data */
    if (os_mbuf_append(ctxt->om, &ble_svc_cts_current_time,
		                 sizeof(ble_svc_cts_current_time))) {
	return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return 0;
}

static inline int
ble_svc_cts_current_time_write_access(struct ble_gatt_access_ctxt *ctxt)
{
    int rc = 0;

    /* Retrieve data */
    uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
    if (om_len != sizeof(ble_svc_cts_current_time)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (ble_hs_mbuf_to_flat(ctxt->om,
			    &ble_svc_cts_current_time, om_len, NULL)) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    
    /* Convert to timeval */
    struct clocktime   ct;
    struct os_timezone tz;
    struct os_timeval  tv;

    if (os_gettimeofday(NULL, &tz)) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    
    ct.year = le16toh(ble_svc_cts_current_time.year);
    ct.mon  = ble_svc_cts_current_time.month;
    ct.day  = ble_svc_cts_current_time.day;
    ct.hour = ble_svc_cts_current_time.hours;
    ct.min  = ble_svc_cts_current_time.minutes;
    ct.sec  = ble_svc_cts_current_time.secondes;
    ct.usec = (ble_svc_cts_current_time.fraction256 * 1000000) / 256;

    if (clocktime_to_timeval(&ct, &tz, &tv)) {
	return BLE_ATT_ERR_UNLIKELY;
    }

    /* Are we ignoring the day of week */
    if (ble_svc_cts_current_time.day_of_week) {
	if (timeval_to_clocktime(&tv, &tz, &ct)) {
	    return BLE_ATT_ERR_UNLIKELY;
	}
	if (ct.dow != (ble_svc_cts_current_time.day_of_week % 7)) {
	    rc = 0x80; // Data field ignored
	}
    }

    /* Apply time change */
    if (os_settimeofday(&tv, NULL)) {
	return BLE_ATT_ERR_UNLIKELY;
    }

    /* Notify 
     *  (Manual change are always notify, don't bother hinting on the offset)
     */
    ble_svc_cts_reference_time_updated(BLE_SVC_CTS_TIME_SOURCE_MANUAL,
				       BLE_SVC_CTS_TIME_ACCURACY_UNKNOWN, 0);

    return rc;
}

#if MYNEWT_VAL(BLE_SVC_CTS_LOCAL_TIME_INFORMATION_READ_PERM) >= 0
static inline int
ble_svc_cts_local_time_information_read_access(struct ble_gatt_access_ctxt *ctxt)
{
    /* Get current timezone/dst */
    struct os_timezone tz;
    if (os_gettimeofday(NULL, &tz)) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Ensure convertion is possible 
     * For DST, see: https://github.com/apache/mynewt-core/issues/944
     */
    if ((tz.tz_minuteswest %   15) ||
	(tz.tz_minuteswest < -720) ||
	(tz.tz_minuteswest >  840)) {
        return BLE_ATT_ERR_UNLIKELY; /* XXX: better error code? */
    }
        
    /* Prepare data */
    ble_svc_cts_local_time_information.time_zone  = tz.tz_minuteswest / 15;
    ble_svc_cts_local_time_information.dst_offset = tz.tz_dsttime ? 4 : 0;

    /* Copy data */
    if (os_mbuf_append(ctxt->om, &ble_svc_cts_local_time_information,
		                 sizeof(ble_svc_cts_local_time_information))) {
	return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    
    return 0;
}

#if MYNEWT_VAL(BLE_SVC_CTS_LOCAL_TIME_INFORMATION_WRITE_PERM) >= 0
static inline int
ble_svc_cts_local_time_information_write_access(struct ble_gatt_access_ctxt *ctxt)
{
    /* Retrieve data */
    uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
    if (om_len > sizeof(ble_svc_cts_local_time_information)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (ble_hs_mbuf_to_flat(ctxt->om,
			    &ble_svc_cts_local_time_information, om_len, NULL)){
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Get current timezone/dst */
    struct os_timezone tz_o;
    if (os_gettimeofday(NULL, &tz_o)) {
	return BLE_ATT_ERR_UNLIKELY;
    }
    
    /* Compute new timezone/dst */
    struct os_timezone tz_n = {
	.tz_minuteswest = ble_svc_cts_local_time_information.time_zone * 15,
	.tz_dsttime     = ble_svc_cts_local_time_information.dst_offset != 0
    };

    /* Ensure conversion is legit 
     * For DST, see: https://github.com/apache/mynewt-core/issues/944
     */
    if (ble_svc_cts_local_time_information.dst_offset != 4) {
        return BLE_ATT_ERR_UNLIKELY; /* XXX: better error code? */
    }
	
    /* Set new timezone/dst */
    if (os_settimeofday(NULL, &tz_n)) 
	return BLE_ATT_ERR_UNLIKELY;

#if MYNEWT_VAL(BLE_SVC_CTS_CURRENT_TIME_NOTIFY_ENABLE) > 0
    /* Notify */
    ble_svc_cts_adjust_reason = 0;
    if (tz_o.tz_minuteswest != tz_n.tz_minuteswest) {
	ble_svc_cts_adjust_reason |=
	    BLE_SVC_CTS_ADJUST_REASON_CHANGE_OF_TIMEZONE;
    }
    if ((tz_o.tz_dsttime != 0) != (tz_n.tz_dsttime != 0)) {
	ble_svc_cts_adjust_reason |=
	    BLE_SVC_CTS_ADJUST_REASON_CHANGE_OF_DST;
    }
    ble_gattc_notify(ble_svc_cts_conn_handle,
		     ble_svc_cts_current_time_handle);
#endif
    
    return 0;
}
#endif
#endif

#if MYNEWT_VAL(BLE_SVC_CTS_REFERENCE_TIME_INFORMATION_READ_PERM) >= 0
static inline int
ble_svc_cts_reference_time_information_read_access(struct ble_gatt_access_ctxt *ctxt)
{
    int rc;

    /* Check that reference time has been used at least once to update
     * time, otherwise use the initial value, which is unknown 
     */
    if (ble_svc_cts_reference_time_was_used) {
	/* Get current time */
	struct os_timeval  tv;
	struct os_timezone tz;
	if (os_gettimeofday(&tv, &tz)) {
	    return BLE_ATT_ERR_UNLIKELY;
	}

	/* Compute delta since last update */
	uint64_t delta = tv.tv_sec - ble_svc_cts_reference_time_last_used; 

	/* Convert to days and hours format */
	uint8_t  days, hours;
	delta /= 3600; 
	hours  = delta % 24;
	delta /= 24;
	days   = delta;

	/* Special case if more than 254 days ago */
	if (delta > 254) {
	    days = hours = BLE_SVC_CTS_UPDATED_SINCE_A_LONG_TIME_AGO;
	}
	
	/* Complete reference time information */
	ble_svc_cts_reference_time_information.days  = days;
	ble_svc_cts_reference_time_information.hours = hours;
    }
    
    /* Copy data */
    rc = os_mbuf_append(ctxt->om,
			&ble_svc_cts_reference_time_information,
			sizeof(ble_svc_cts_reference_time_information));

    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
#endif

/**
 * CTS access function
 */
static int
ble_svc_cts_access(uint16_t conn_handle, uint16_t attr_handle,
		   struct ble_gatt_access_ctxt *ctxt,
		   void *arg)
{
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);
    int rc = 0;
    
    switch (uuid16) {
    case BLE_SVC_CTS_CHR_UUID16_CURRENT_TIME:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = ble_svc_cts_current_time_read_access(ctxt);
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = ble_svc_cts_current_time_write_access(ctxt);
        } else {
            assert(0);
        }
	return rc;

#if MYNEWT_VAL(BLE_SVC_CTS_LOCAL_TIME_INFORMATION_READ_PERM) >= 0
    case BLE_SVC_CTS_CHR_UUID16_LOCAL_TIME_INFORMATION:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = ble_svc_cts_local_time_information_read_access(ctxt);
#if MYNEWT_VAL(BLE_SVC_CTS_LOCAL_TIME_INFORMATION_WRITE_PERM) >= 0
        } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = ble_svc_cts_local_time_information_write_access(ctxt);
#endif
        } else {
            assert(0);
        }
	return rc;
#endif
	
#if MYNEWT_VAL(BLE_SVC_CTS_REFERENCE_TIME_INFORMATION_READ_PERM) >= 0
    case BLE_SVC_CTS_CHR_UUID16_REFERENCE_TIME_INFORMATION:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = ble_svc_cts_reference_time_information_read_access(ctxt);
	} else {
	    assert(0);
	}
	return rc;
#endif
	
    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/**
 * This function must be called with the connection handle when a gap 
 * connect event is received in order to send notifications to the
 * client.
 *
 * @params conn_handle          The connection handle for the current
 *                                  connection.
 */
void 
ble_svc_cts_on_gap_connect(uint16_t conn_handle) 
{
    ble_svc_cts_conn_handle = conn_handle;
}

/**
 * Initialize the Current Time Service.
 * 
 * @return 0 on success, non-zero error code otherwise.
 */
void
ble_svc_cts_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = ble_gatts_count_cfg(ble_svc_cts_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gatts_add_svcs(ble_svc_cts_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);
}

/**
 * Set information about the source used as reference time.
 * @param source                One of:
 *                               o BLE_SVC_CTS_TIME_SOURCE_UNKNOWN
 *                               o BLE_SVC_CTS_TIME_SOURCE_NETWORK_TIME_PROTOCOL
 *                               o BLE_SVC_CTS_TIME_SOURCE_GPS
 *                               o BLE_SVC_CTS_TIME_SOURCE_RADIO_TIME_SIGNAL
 *                               o BLE_SVC_CTS_TIME_SOURCE_MANUAL
 *                               o BLE_SVC_CTS_TIME_SOURCE_ATOMIC_CLOCK
 *                               o BLE_SVC_CTS_TIME_SOURCE_CELLULAR_NETWORK
 * @param accuracy		Accuracy in 125ms step (0-253), or:
 *                               o BLE_SVC_CTS_TIME_ACCURACY_OUT_OF_RANGE
 *                               o BLE_SVC_CTS_TIME_ACCURACY_UNKOWN.
 * @param offset                Indicative amount of time that as been 
 *                              adjusted:
 *                               o values lesser than -120 or greater then 120
 *                                 are used to indicate an offset of more
 *                                 than 2 minutes
 *                               o a value of 0 indicate that the offset
 *                                 was unknown
 */
int
ble_svc_cts_reference_time_updated(uint8_t source, uint8_t accuracy,
				   int8_t offset) {
    int rc = 0;

    /* Sanity check */
    if (source >= 7) {
	return -1;
    }

    /* Get current time */
    struct os_timeval  tv;
    rc = os_gettimeofday(&tv, NULL);
    if (rc != 0) {
	return rc;
    }

    /* Mark that we have perform an update */
    ble_svc_cts_reference_time_was_used = true;
    
    /* Save source / accuracy */
    ble_svc_cts_reference_time_information.time_source   = source;
    ble_svc_cts_reference_time_information.time_accuracy = accuracy;

#if MYNEWT_VAL(BLE_SVC_CTS_CURRENT_TIME_NOTIFY_ENABLE) > 0
    /* Notify? 
     * ???: "The update was caused by the client device
     *       (interacting with another service)."
     */
    bool notify = (ble_svc_cts_reference_time_last_used - tv.tv_sec) > (60*15);
    if (offset && ((offset < -60) || (offset > 60))) {
	notify = true;
    }
    if (source == BLE_SVC_CTS_TIME_SOURCE_MANUAL) {
	notify = true;
    }

    switch(source) {
    case BLE_SVC_CTS_TIME_SOURCE_NETWORK_TIME_PROTOCOL:
    case BLE_SVC_CTS_TIME_SOURCE_GPS:
    case BLE_SVC_CTS_TIME_SOURCE_RADIO_TIME_SIGNAL:
    case BLE_SVC_CTS_TIME_SOURCE_ATOMIC_CLOCK:
    case BLE_SVC_CTS_TIME_SOURCE_CELLULAR_NETWORK:
	ble_svc_cts_adjust_reason = 
	    BLE_SVC_CTS_ADJUST_REASON_EXTERN_REFERENCE_TIME_UPDATE;
	break;
    case BLE_SVC_CTS_TIME_SOURCE_MANUAL:
	ble_svc_cts_adjust_reason = 
	    BLE_SVC_CTS_ADJUST_REASON_MANUAL_TIME_UPDATE;
	break;
    case BLE_SVC_CTS_TIME_SOURCE_UNKNOWN:
    default:
	ble_svc_cts_adjust_reason = 0;
	break;
    }

    if (notify) {
	ble_gattc_notify(ble_svc_cts_conn_handle,
			 ble_svc_cts_current_time_handle);
    }
#endif
    
    /* Save updated time for later reference */
    ble_svc_cts_reference_time_last_used = tv.tv_sec;
	
    return 0;
};
	
