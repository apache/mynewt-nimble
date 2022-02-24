/*
 * Copyright (c) 2019 Oticon A/S
 * Copyright (c) 2021 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "bs_tracing.h"
#include "bs_oswrap.h"
#include "bs_pc_base_fifo_user.h"

#include <time_machine.h>
#include "edtt_driver.h"
#include "os/os_sched.h"

/* Recheck if something arrived from the EDTT every 5ms */
#define EDTT_IF_RECHECK_DELTA 5 /* ms */

/* We want the runs to be deterministic => we want to resync with the Phy
 * before we retry any read so the bridge device may also run
 */
#define EDTT_SIMU_RESYNC_TIME_WITH_EDTT \
	(EDTT_IF_RECHECK_DELTA * 1000 - 1)

static int edtt_mode_enabled = 0;

/* In this mode, when the EDTTool closes the FIFO we automatically terminate
 * this simulated device. If false, we just continue running
 */
static int edtt_autoshutdown;

#define TO_DEVICE  0
#define TO_BRIDGE 1
static int fifo[2] = { -1, -1 };
static char *fifo_path[2] = {NULL, NULL};

extern unsigned int global_device_nbr;

static void edttd_clean_up(void);
static void edptd_create_fifo_if(void);
static int fifo_low_level_read(uint8_t *bufptr, int size);

bool edtt_start(void)
{
	if (edtt_mode_enabled == false) {
		/* otherwise we don't try to open the EDTT interface */
		return true;
	}

	edptd_create_fifo_if();

	extern void tm_set_phy_max_resync_offset(uint64_t offset_in_us);
	tm_set_phy_max_resync_offset(EDTT_SIMU_RESYNC_TIME_WITH_EDTT);
	return true;
}

bool edtt_stop(void)
{
	if (edtt_mode_enabled == false) {
		/* otherwise we don't try to open the EDTT interface */
		return true;
	}

	bs_trace_raw(9, "EDTTT: %s called\n", __func__);
	edttd_clean_up();
	edtt_mode_enabled = false;
	return true;
}

static void
print_hex_array(int lvl, uint8_t *buf, int len)
{
    char str[2*len];
    char *p = str;
    int i;
    for (i = 0; i < len; i++) {
        if (i > 0) *p++ = ' ';
        sprintf(p++, "%02X", buf[i]);
    }
    *p = '\n';
    bs_trace_raw(lvl, str);
}

/**
 * Attempt to read size bytes thru the EDTT IF into the buffer <*ptr>
 * <flags> can be set to EDTTT_BLOCK or EDTTT_NONBLOCK
 *
 * If set to EDTTT_BLOCK it will block the calling thread until <size>
 * bytes have been read or the interface has been closed.
 * If set to EDTTT_NONBLOCK it returns as soon as there is no more data to be
 * read
 *
 * Returns the amount of read bytes, or -1 on error
 */
int edtt_read(uint8_t *ptr, size_t size, int flags)
{
    uint8_t *buf = ptr;

	if (edtt_mode_enabled == false) {
		return -1;
	}

	bs_trace_raw_time(8, "EDTT: Asked to read %i bytes\n", size);
	int read = 0;

	while (size > 0) {
		int received_bytes;

		received_bytes = fifo_low_level_read(ptr, size);
		if (received_bytes < 0) {
			return -1;
		} else if (received_bytes > 0) {
			size -= received_bytes;
			ptr += received_bytes;
			read += received_bytes;
		} else {
			if (flags & EDTTT_BLOCK) {
				bs_trace_raw_time(9, "EDTT: No enough data yet,"
						"sleeping for %i ms\n",
						EDTT_IF_RECHECK_DELTA);
                os_sched_sleep(os_sched_get_current_task(),
                               os_time_ms_to_ticks32(EDTT_IF_RECHECK_DELTA));
                os_sched(NULL);
			} else {
				bs_trace_raw_time(9, "EDTT: No enough data yet,"
						"returning\n");
				break;
			}
		}
    }

    if (read > 0) {
        bs_trace_raw_time(8, "Read %i bytes:\n", read);
        print_hex_array(8, buf, read);
    }
	return read;
}

/**
 * Write <size> bytes from <ptr> toward the EDTTool
 *
 * <flags> is ignored in this driver, all writes to the tool are
 * instantaneous
 */
int edtt_write(uint8_t *ptr, size_t size, int flags)
{
	if (edtt_mode_enabled == false) {
		return -1;
	}

	bs_trace_raw_time(6, "EDTT: Asked to write %i bytes: ", size);
    print_hex_array(6, ptr, size);

	if (write(fifo[TO_BRIDGE], ptr, size) != size) {
		if (errno == EPIPE) {
			bs_trace_error_line("EDTT IF suddenly closed by other "
					    "end\n");
		}
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			bs_trace_error_line("EDTT IF to bridge filled up (FIFO "
					    "size needs to be increased)\n");
		}
		bs_trace_error_line("EDTT IF: Unexpected error on write\n");
	}
	return size;
}

/*
 * Applications may want to enable the EDTT interface only in some
 * cases. By default it is not enabled in this driver. This function
 * must be called once before starting it to do so
 */
void enable_edtt_mode(void)
{
	edtt_mode_enabled = true;
}

/**
 * Automatically terminate this device execution once the EDTTool disconnects
 */
void set_edtt_autoshutdown(bool Mode)
{
	edtt_autoshutdown = Mode;
}

static void edptd_create_fifo_if(void)
{
	int flags;

	bs_trace_raw_time(9, "Bringing EDTT IF up (waiting for other side)\n");

	if (pb_com_path == NULL) {
		bs_trace_error_line("Not connected to Phy."
				    "EDTT IF cannot be brough up\n");
	}

	/* At this point we have connected to the Phy so the COM folder does
	 * already exist
	 * also SIGPIPE is already ignored
	 */

	fifo_path[TO_DEVICE] = (char *)bs_calloc(pb_com_path_length + 30,
						 sizeof(char));
	fifo_path[TO_BRIDGE] = (char *)bs_calloc(pb_com_path_length + 30,
						 sizeof(char));
	sprintf(fifo_path[TO_DEVICE], "%s/Device%i.PTTin",
		pb_com_path, global_device_nbr);
	sprintf(fifo_path[TO_BRIDGE], "%s/Device%i.PTTout",
		pb_com_path, global_device_nbr);

	if ((pb_create_fifo_if_not_there(fifo_path[TO_DEVICE]) != 0)
		|| (pb_create_fifo_if_not_there(fifo_path[TO_BRIDGE]) != 0)) {
		bs_trace_error_line("Couldnt create FIFOs for EDTT IF\n");
	}

	/* we block here until the bridge opens its end */
	fifo[TO_BRIDGE] = open(fifo_path[TO_BRIDGE], O_WRONLY);
	if (fifo[TO_BRIDGE] == -1) {
		bs_trace_error_line("Couldn't create FIFOs for EDTT IF\n");
	}

	flags = fcntl(fifo[TO_BRIDGE], F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(fifo[TO_BRIDGE], F_SETFL, flags);

	/* we will block here until the bridge opens its end */
	fifo[TO_DEVICE] = open(fifo_path[TO_DEVICE], O_RDONLY);
	if (fifo[TO_DEVICE] == -1) {
		bs_trace_error_line("Couldn't create FIFOs for EDTT IF\n");
	}

	flags = fcntl(fifo[TO_DEVICE], F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(fifo[TO_DEVICE], F_SETFL, flags);
}

static void edttd_clean_up(void)
{
	for (int dir = TO_DEVICE ; dir <= TO_BRIDGE ; dir++) {
		if (fifo_path[dir]) {
			if (fifo[dir] != -1) {
				close(fifo[dir]);
				remove(fifo_path[dir]);
				fifo[dir] = -1;
			}
			free(fifo_path[dir]);
			fifo_path[dir] = NULL;
		}
	}
	if (pb_com_path != NULL) {
		rmdir(pb_com_path);
	}
}

static int fifo_low_level_read(uint8_t *bufptr, int size)
{
	int received_bytes = read(fifo[TO_DEVICE], bufptr, size);

	if ((received_bytes == -1) && (errno == EAGAIN)) {
		return 0;
	} else if (received_bytes == EOF || received_bytes == 0) {
		/*The FIFO was closed by the bridge*/
		if (edtt_autoshutdown) {
			bs_trace_raw_time(3, "EDTT: FIFO closed "
					"(ptt_autoshutdown==true) =>"
					" Terminate\n");
			edttd_clean_up();
			bs_trace_exit_line("\n");
		} else {
			bs_trace_raw_time(3, "EDTT: FIFO closed "
					"(ptt_autoshutdown==false) => We close "
					"the FIFOs and move on\n");
			edttd_clean_up();
			edtt_mode_enabled = false;
			return -1;
		}
	} else if (received_bytes == -1) {
		bs_trace_error_line("EDTT: Unexpected error\n");
	}

	return received_bytes;
}
