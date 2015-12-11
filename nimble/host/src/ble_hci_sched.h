#ifndef H_BLE_HCI_SCHED_
#define H_BLE_HCI_SCHED_

typedef int ble_hci_sched_tx_fn(void *arg);

int ble_hci_sched_enqueue(ble_hci_sched_tx_fn *tx_cb, void *tx_cb_arg);
void ble_hci_sched_wakeup(void);
void ble_hci_sched_command_complete(void);
int ble_hci_sched_init(void);

#endif
