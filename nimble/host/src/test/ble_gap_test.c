#include <string.h>
#include "os/os.h"
#include "testutil/testutil.h"
#include "nimble/hci_common.h"
#include "nimble/hci_transport.h"
#include "host/ble_hs.h"
#include "host/ble_hs_test.h"
#include "ble_hs_test_util.h"
#include "ble_hs_conn.h"
#include "ble_hs_work.h"
#include "ble_gap_conn.h"
#include "ble_gap.h"

#ifdef ARCH_sim
#define BLE_GAP_TEST_STACK_SIZE     1024
#else
#define BLE_GAP_TEST_STACK_SIZE     256
#endif

#define BLE_GAP_TEST_HS_PRIO        10

static struct os_task ble_gap_test_task;
static os_stack_t ble_gap_test_stack[OS_STACK_ALIGN(BLE_GAP_TEST_STACK_SIZE)];

static void
ble_gap_test_misc_rx_ack(uint16_t ocf, uint8_t status)
{
    uint16_t opcode;
    uint8_t buf[BLE_HCI_EVENT_CMD_STATUS_LEN];
    int rc;

    opcode = (BLE_HCI_OGF_LE << 10) | ocf;
    ble_hs_test_util_build_cmd_status(buf, sizeof buf, status, 1, opcode);

    rc = ble_hci_transport_ctlr_event_send(buf);
    TEST_ASSERT(rc == 0);
}

static void 
ble_gap_test_task_handler(void *arg) 
{
    struct hci_le_conn_complete evt;
    uint8_t addr[6] = { 1, 2, 3, 4, 5, 6 };
    int rc;

    TEST_ASSERT(!ble_hs_work_busy);
    TEST_ASSERT(ble_hs_conn_first() == NULL);

    ble_gap_direct_connection_establishment(0, addr);
    TEST_ASSERT(ble_hs_work_busy);
    TEST_ASSERT(ble_hs_conn_first() == NULL);

    ble_gap_test_misc_rx_ack(BLE_HCI_OCF_LE_CREATE_CONN, 0);
    TEST_ASSERT(!ble_hs_work_busy);
    TEST_ASSERT(ble_hs_conn_first() == NULL);

    memset(&evt, 0, sizeof evt);
    evt.subevent_code = BLE_HCI_LE_SUBEV_CONN_COMPLETE;
    evt.status = BLE_ERR_SUCCESS;
    evt.connection_handle = 2;
    memcpy(evt.peer_addr, addr, 6);
    rc = ble_gap_conn_rx_conn_complete(&evt);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(ble_hs_conn_find(2) != NULL);

    tu_restart();
}

TEST_CASE(ble_gap_test_case)
{
    os_init();

    os_task_init(&ble_gap_test_task, "ble_gap_test_task",
                 ble_gap_test_task_handler, NULL,
                 BLE_GAP_TEST_HS_PRIO + 1, OS_WAIT_FOREVER, ble_gap_test_stack,
                 OS_STACK_ALIGN(BLE_GAP_TEST_STACK_SIZE));

    ble_hs_init(BLE_GAP_TEST_HS_PRIO);

    os_start();
}

TEST_SUITE(ble_gap_test_suite)
{
    ble_gap_test_case();
}

int
ble_gap_test_all(void)
{
    ble_gap_test_suite();
    return tu_any_failed;
}

