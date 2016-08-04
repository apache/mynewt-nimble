#ifndef H_BLE_HCI_UART_
#define H_BLE_HCI_UART_

struct ble_hci_uart_cfg {
    uint32_t baud;
    uint16_t num_evt_bufs;
    uint16_t evt_buf_sz;
    uint8_t uart_port;
    uint8_t flow_ctrl;
    uint8_t data_bits;
    uint8_t stop_bits;
    uint8_t parity;
};

extern const struct ble_hci_uart_cfg ble_hci_uart_cfg_dflt;

int ble_hci_uart_init(const struct ble_hci_uart_cfg *cfg);

#endif
