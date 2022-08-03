#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

import logging
import asyncio
import struct
import hci
import sys
import time


async def wait_ev(ev):
    while ev.is_set() == False:
        await asyncio.sleep(0.000001)


async def wait_for_event(ev, timeout):
    try:
        await asyncio.wait_for(wait_ev(ev), timeout)
    except TimeoutError as e:
        logging.error(f"Timeout waiting for event: {e}")
        sys.exit()


class HCI_Commands():
    def __init__(self, send=None, rx_buffer_q=None,
                 asyncio_loop=None, tp=None, device_mode="rx"):
        self.hci_send_cmd = hci.HCI_Cmd_Send()
        self.hci_send_acl_data = hci.HCI_ACL_Data_Send()
        self.hci_recv_ev_packet = hci.HCI_Recv_Event_Packet()
        self.async_sem_cmd = asyncio.Semaphore()
        self.async_ev_cmd_end = asyncio.Event()
        self.async_ev_connected = asyncio.Event()
        self.async_ev_encryption_change = asyncio.Event()
        self.async_ev_set_data_len = asyncio.Event()
        self.async_ev_update_phy = asyncio.Event()
        self.async_ev_num_cmp_pckts = asyncio.Event()
        self.async_ev_recv_data_finish = asyncio.Event()
        self.async_ev_rx_wait_finish = asyncio.Event()
        self.async_lock_packets_cnt = asyncio.Lock()
        self.valid_recv_data = 0
        self.expected_recv_data = 0
        self.last_timestamp = 0
        self.sent_packets_counter = 0

        self.send = send
        self.rx_buffer_q = rx_buffer_q
        self.tp = tp
        self.loop = asyncio_loop
        self.device_mode = device_mode

    async def rx_buffer_q_wait(self):
        try:
            logging.debug("%s", self.rx_buffer_q_wait.__name__)
            while not self.async_ev_rx_wait_finish.is_set():
                if self.rx_buffer_q.empty():
                    await asyncio.sleep(0.000000001)
                    continue
                await self.loop.create_task(self.recv_handler())
            logging.info("rx_buffer_q_wait finished")
            self.async_ev_rx_wait_finish.clear()
        except asyncio.CancelledError:
            logging.critical("rx_buffer_q_wait task canceled")

    """ 7.3 Controller & Baseband commands """
    async def cmd_set_event_mask(self, mask: int = 0x00001fffffffffff):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_HOST_CTL, hci.OCF_SET_EVENT_MASK,
                                  struct.pack('<Q', mask))
            logging.debug("%s %s", self.cmd_set_event_mask.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_reset(self):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_HOST_CTL, hci.OCF_RESET)
            logging.debug("%s %s", self.cmd_reset.__name__, self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    """ 7.4 Informational parameters """
    async def cmd_read_local_supported_cmds(self):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_INFO_PARAM,
                                  hci.OCF_READ_LOCAL_COMMANDS)
            logging.debug("%s %s", self.cmd_read_local_supported_cmds.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_read_bd_addr(self):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_INFO_PARAM, hci.OCF_READ_BD_ADDR)
            logging.debug("%s %s", self.cmd_read_bd_addr.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    """ 7.8 LE Controller Commands """
    async def cmd_le_set_event_mask(self, mask: int = 0x000000000000001f):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_LE_CTL, hci.OCF_LE_SET_EVENT_MASK,
                                  struct.pack('<Q', mask))
            logging.debug("%s %s", self.cmd_le_set_event_mask.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_read_buffer_size(self):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_LE_CTL,
                                  hci.OCF_LE_READ_BUFFER_SIZE_V1)
            logging.debug("%s %s", self.cmd_le_read_buffer_size.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_read_local_supported_features(self):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_LE_CTL,
                                  hci.OCF_LE_READ_LOCAL_SUPPORTED_FEATURES)
            logging.debug("%s %s",
                          self.cmd_le_read_local_supported_features.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_set_random_addr(self, addr: str):
        async with self.async_sem_cmd:
            addr_ba = hci.cmd_addr_to_ba(addr)
            self.hci_send_cmd.set(hci.OGF_LE_CTL,
                                  hci.OCF_LE_SET_RANDOM_ADDRESS,
                                  addr_ba)
            logging.debug("%s %s", self.cmd_le_set_random_addr.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_set_advertising_params(self, adv_params: hci.HCI_Advertising):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_LE_CTL,
                                  hci.OCF_LE_SET_ADVERTISING_PARAMETERS,
                                  adv_params.ba_full_message)
            logging.debug("%s %s", self.cmd_le_set_advertising_params.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_set_advertising_enable(self, adv_en: int = 0):
        """ Default: Disabled """
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_LE_CTL,
                                  hci.OCF_LE_SET_ADVERTISE_ENABLE,
                                  struct.pack('<B', adv_en))
            logging.debug("%s %s", self.cmd_le_set_advertising_enable.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_set_scan_params(self, scan_params: hci.HCI_Scan):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(
                hci.OGF_LE_CTL,
                hci.OCF_LE_SET_SCAN_PARAMETERS,
                scan_params.ba_full_message)
            logging.debug("%s %s", self.cmd_le_set_scan_params.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_set_scan_enable(self, scan_en: int = 0, filter_dup: int = 0):
        """ Default:
            scan_en: disabled
            filter_dup: disabled
        """
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_LE_CTL, hci.OCF_LE_SET_SCAN_ENABLE,
                                  struct.pack('<BB', scan_en, filter_dup))
            logging.debug("%s %s", self.cmd_le_set_scan_enable.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_create_connection(self, con_params: hci.HCI_Connect):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_LE_CTL, hci.OCF_LE_CREATE_CONN,
                                  con_params.ba_full_message)
            logging.debug("%s %s", self.cmd_le_create_connection.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_enable_encryption(self, conn_handle: int, random_number: int, ediv: int, ltk: int):
        async with self.async_sem_cmd:
            hci.ltk = ltk
            random_number_bytes = random_number.to_bytes(8, byteorder='little')
            ltk_bytes = ltk.to_bytes(16, byteorder='little')
            data_bytes = struct.pack("<H", conn_handle) + random_number_bytes + \
                struct.pack("<H", ediv) + ltk_bytes
            self.hci_send_cmd.set(
                hci.OGF_LE_CTL,
                hci.OCF_LE_ENABLE_ENCRYPTION,
                data_bytes)
            logging.debug("%s %s", self.cmd_le_enable_encryption.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_long_term_key_request_reply(self, conn_handle: int, ltk: int):
        async with self.async_sem_cmd:
            ltk_bytes = ltk.to_bytes(16, byteorder='little')
            data_bytes = struct.pack('<H', conn_handle) + ltk_bytes
            self.hci_send_cmd.set(
                hci.OGF_LE_CTL,
                hci.OCF_LE_LONG_TERM_KEY_REQUEST_REPLY,
                data_bytes)
            logging.debug(
                "%s %s",
                self.cmd_le_long_term_key_request_reply.__name__,
                self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            # Is run from another command,
            # don't need to wait for cmd complete.
            # await self.async_ev_cmd_end.wait()
            # self.async_ev_cmd_end.clear()

    async def cmd_le_set_data_len(self, conn_handle: int, tx_octets: int, tx_time: int):
        """ conn_handle: Range 0x0000 to 0x0EFF
            tx_octets: Range 0x001B to 0x00FB
            tx_time: Range 0x0148 to 0x4290
        """
        logging.debug("%s", self.cmd_le_set_data_len.__name__)
        async with self.async_sem_cmd:
            if tx_octets == 0 or tx_time == 0:
                tx_octets = hci.max_data_len.supported_max_tx_octets
                tx_time = hci.max_data_len.supported_max_tx_time
            hci.requested_tx_octets = tx_octets
            hci.requested_tx_time = tx_time
            while conn_handle != hci.conn_handle:
                await asyncio.sleep(0.001)
            self.hci_send_cmd.set(hci.OGF_LE_CTL, hci.OCF_LE_SET_DATA_LEN,
                                  struct.pack('<HHH', conn_handle,
                                              tx_octets, tx_time))
            logging.debug("%s %s", self.cmd_le_set_data_len.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_read_suggested_dflt_data_len(self):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_LE_CTL,
                                  hci.OCF_LE_READ_SUGGESTED_DFLT_DATA_LEN)
            logging.debug(
                "%s %s",
                self.cmd_le_read_suggested_dflt_data_len.__name__,
                self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_read_max_data_len(self):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_LE_CTL,
                                  hci.OCF_LE_READ_MAX_DATA_LEN)
            logging.debug("%s %s", self.cmd_le_read_max_data_len.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_read_phy(self, conn_handle: int):
        """ conn_handle: Range 0x0000 to 0x0EFF
        """
        async with self.async_sem_cmd:
            while conn_handle != hci.conn_handle:
                await asyncio.sleep(0.001)
            self.hci_send_cmd.set(hci.OGF_LE_CTL, hci.OCF_LE_READ_PHY,
                                  struct.pack('<H', conn_handle))
            logging.debug("%s %s", self.cmd_le_read_phy.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_set_dflt_phy(self, all_phys: int = 0, tx_phys: int = 0, rx_phys: int = 0):
        """ Default:
            all_phys: 0 - The Host has no preference among the transmitter PHYs
                          supported by the Controller
            tx_phys: 0 - The Host prefers to use the LE 1M transmitter PHY
                         (possibly among others)
            rx_phys: 0 - The Host prefers to use the LE 1M receiver PHY
                         (possibly among others)
        """
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_LE_CTL, hci.OCF_LE_SET_DFLT_PHY,
                                  struct.pack('<BBB', all_phys,
                                              tx_phys, rx_phys))
            logging.debug("%s %s", self.cmd_le_set_dflt_phy.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_le_set_phy(self, conn_handle: int, all_phys: int = 0,
                             tx_phys: int = 0, rx_phys: int = 0,
                             phy_options: int = 0):
        """ Default:
            conn_handle: Range 0x0000 to 0x0EFF
            all_phys: The Host has no preference among the transmitter PHYs
                      supported by the Controller
            tx_phys: 0 - The Host prefers to use the LE 1M transmitter PHY
                         (possibly among others)
            rx_phys: 0 - The Host prefers to use the LE 1M receiver PHY
                         (possibly among others)
            phy_options: 0 - the Host has no preferred coding when
                             transmitting on the LE Coded PHY
        """
        async with self.async_sem_cmd:
            while conn_handle != hci.conn_handle:
                await asyncio.sleep(0.001)
            self.hci_send_cmd.set(hci.OGF_LE_CTL, hci.OCF_LE_SET_PHY,
                                  struct.pack('<HBBBH', conn_handle, all_phys,
                                              tx_phys, rx_phys, phy_options))
            logging.debug("%s %s", self.cmd_le_set_phy.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    async def cmd_vs_read_static_addr(self):
        async with self.async_sem_cmd:
            self.hci_send_cmd.set(hci.OGF_VENDOR_SPECIFIC,
                                  hci.BLE_HCI_OCF_VS_RD_STATIC_ADDR)
            logging.debug("%s %s", self.cmd_vs_read_static_addr.__name__,
                          self.hci_send_cmd)
            await self.send(self.hci_send_cmd.ba_full_message)
            await self.async_ev_cmd_end.wait()
            self.async_ev_cmd_end.clear()

    """ Send data """
    async def acl_data_send(self, acl_data: hci.HCI_ACL_Data_Send):
        async with self.async_sem_cmd:
            acl_data.connection_handle = hci.conn_handle
            self.hci_send_acl_data = acl_data
            await self.send(self.hci_send_acl_data.ba_full_message)
            self.sent_packets_counter += 1

    """ Parse and process received data"""

    def parse_ev_disconn_cmp(self, data: bytes):
        ev_disconn_cmp = hci.HCI_Ev_Disconn_Complete()
        ev_disconn_cmp.set(*struct.unpack('<BHB', bytes(data[:4])))
        return ev_disconn_cmp

    def parse_ev_cmd_cmp(self, data: bytes):
        ev_cmd_cmp = hci.HCI_Ev_Cmd_Complete()
        ev_cmd_cmp.set(*struct.unpack('<BH', bytes(data[:3])), data[3:])
        return ev_cmd_cmp

    def parse_ev_cmd_stat(self, data: bytes):
        ev_cmd_stat = hci.HCI_Ev_Cmd_Status()
        ev_cmd_stat.set(*struct.unpack('<BBH', bytes(data[:4])))
        return ev_cmd_stat

    def parse_ev_encryption_change(self, data: bytes):
        ev_encryption_change = hci.HCI_Ev_LE_Encryption_Change()
        ev_encryption_change.set(*struct.unpack('<BHB', bytes(data[:4])))
        return ev_encryption_change

    def parse_ev_le_meta(self, data: bytes):
        ev_le_meta = hci.HCI_Ev_LE_Meta()
        ev_le_meta.set(data[0])
        return ev_le_meta

    def parse_subev_le_enhcd_conn_cmp(self, data: bytes):
        ev_le_enhcd_conn_cmp = hci.HCI_Ev_LE_Enhanced_Connection_Complete()
        ev_le_enhcd_conn_cmp.set(*struct.unpack('<BBHBB', bytes(data[:6])),
                                 hci.ba_addr_to_str(bytes(data[6:12])),
                                 hci.ba_addr_to_str(bytes(data[12:18])),
                                 hci.ba_addr_to_str(bytes(data[18:24])),
                                 *struct.unpack('<HHHB', bytes(data[24:])))
        return ev_le_enhcd_conn_cmp

    def parse_subev_le_data_len_change(self, data: bytes):
        ev_le_data_len_change = hci.HCI_Ev_LE_Data_Length_Change()
        ev_le_data_len_change.set(*struct.unpack('<BHHHHH', bytes(data[:11])))
        return ev_le_data_len_change

    def parse_subev_le_long_term_key_request(self, data: bytes):
        ev_le_long_term_key_request = hci.HCI_Ev_LE_Long_Term_Key_Request()
        ev_le_long_term_key_request.set(
            *struct.unpack('<BHQH', bytes(data[:13])))
        return ev_le_long_term_key_request

    def parse_subev_le_phy_update_cmp(self, data: bytes):
        le_phy_update_cmp = hci.HCI_Ev_LE_PHY_Update_Complete()
        le_phy_update_cmp.set(*struct.unpack('<BBHBB', data))
        return le_phy_update_cmp

    def parse_subev_le_chan_sel_alg(self, data: bytes):
        le_chan_sel_alg = hci.HCI_Ev_LE_Chan_Sel_Alg()
        le_chan_sel_alg.set(*struct.unpack('<BHB', data))
        return le_chan_sel_alg

    def parse_num_comp_pkts(self, data: bytes):
        hci.ev_num_comp_pkts = hci.HCI_Number_Of_Completed_Packets()
        hci.ev_num_comp_pkts.set(*struct.unpack('<BHH', bytes(data[:5])))
        return hci.ev_num_comp_pkts

    def process_returned_parameters(self):
        def status() -> int:
            current_ev_name = type(
                self.hci_recv_ev_packet.current_event).__name__
            if current_ev_name == type(hci.HCI_Ev_Cmd_Complete()).__name__:
                return struct.unpack_from(
                    "<B", self.hci_recv_ev_packet.current_event.return_parameters, offset=0)[0]
            elif current_ev_name == type(hci.HCI_Ev_Cmd_Status()).__name__:
                return self.hci_recv_ev_packet.current_event.status
            else:
                return -100

        current_ev = self.hci_recv_ev_packet.current_event
        ogf, ocf = hci.get_ogf_ocf(current_ev.opcode)

        if ogf == hci.OGF_HOST_CTL:
            if ocf == hci.OCF_SET_EVENT_MASK:
                return status()
            elif ocf == hci.OCF_RESET:
                return status()

        elif ogf == hci.OGF_INFO_PARAM:
            if ocf == hci.OCF_READ_LOCAL_COMMANDS:
                hci.read_local_commands = hci.Read_Local_Commands()
                hci.read_local_commands.set(
                    bytes(current_ev.return_parameters))
                return status()
            elif ocf == hci.OCF_READ_BD_ADDR:
                hci.bdaddr = hci.ba_addr_to_str(
                    bytes(current_ev.return_parameters[1:7]))
                return status()

        elif ogf == hci.OGF_LE_CTL:
            if ocf == hci.OCF_LE_SET_EVENT_MASK:
                return status()
            elif ocf == hci.OCF_LE_READ_BUFFER_SIZE_V1:
                hci.le_read_buffer_size = hci.LE_Read_Buffer_Size()
                hci.le_read_buffer_size.set(*struct.unpack("<BHB",
                                            current_ev.return_parameters))
                logging.info(f"LE Buffer size: {hci.le_read_buffer_size}")
                return hci.le_read_buffer_size.status
            elif ocf == hci.OCF_LE_READ_LOCAL_SUPPORTED_FEATURES:
                hci.le_read_local_supported_features = hci.LE_Read_Local_Supported_Features()
                hci.le_read_local_supported_features.set(
                    current_ev.return_parameters)
                return status()
            elif ocf == hci.OCF_LE_SET_RANDOM_ADDRESS:
                return status()
            elif ocf == hci.OCF_LE_SET_ADVERTISING_PARAMETERS:
                return status()
            elif ocf == hci.OCF_LE_SET_ADVERTISE_ENABLE:
                return status()
            elif ocf == hci.OCF_LE_SET_SCAN_PARAMETERS:
                return status()
            elif ocf == hci.OCF_LE_SET_SCAN_ENABLE:
                return status()
            elif ocf == hci.OCF_LE_CREATE_CONN:
                return status()
            elif ocf == hci.OCF_LE_SET_DATA_LEN:
                return status()
            elif ocf == hci.OCF_LE_READ_SUGGESTED_DFLT_DATA_LEN:
                hci.suggested_dflt_data_len = hci.Suggested_Dflt_Data_Length()
                hci.suggested_dflt_data_len.set(*struct.unpack("<BHH",
                                                current_ev.return_parameters))
                logging.info(
                    f"Suggested Deafult Data Len: {hci.suggested_dflt_data_len}")
                return status()
            elif ocf == hci.OCF_LE_READ_MAX_DATA_LEN:
                hci.max_data_len = hci.Max_Data_Length()
                hci.max_data_len.set(*struct.unpack("<BHHHH",
                                     current_ev.return_parameters))
                logging.info(f"Suggested Max Data Len: {hci.max_data_len}")
                if (hci.num_of_bytes_to_send >
                        hci.max_data_len.supported_max_tx_octets - hci.L2CAP_HDR_BYTES):
                    logging.critical(
                        f"Number of data bytes to send + 4 bytes of L2CAP header: {hci.num_of_bytes_to_send + 4} "
                        f"exceeds allowed value of: {hci.max_data_len.supported_max_tx_octets}. Closing.")
                    raise SystemExit(
                        f"Number of data bytes to send + 4 bytes of L2CAP header: {hci.num_of_bytes_to_send + 4} "
                        f"exceeds allowed value of: {hci.max_data_len.supported_max_tx_octets}. Closing.")

                return status()
            elif ocf == hci.OCF_LE_READ_PHY:
                hci.phy = hci.LE_Read_PHY()
                hci.phy.set(*struct.unpack('<BHBB',
                            current_ev.return_parameters))
                logging.info(f"Current LE PHY: {hci.phy}")
                return status()
            elif ocf == hci.OCF_LE_SET_DFLT_PHY:
                return status()
            elif ocf == hci.OCF_LE_SET_PHY:
                return status()

        elif ogf == hci.OGF_VENDOR_SPECIFIC:
            if ocf == hci.BLE_HCI_OCF_VS_RD_STATIC_ADDR:
                if type(current_ev).__name__ == type(
                        hci.HCI_Ev_Cmd_Complete()).__name__:
                    hci.static_addr = hci.ba_addr_to_str(
                        bytes(current_ev.return_parameters[1:7]))
                    logging.info(f"Received rd static addr: {hci.static_addr}")
                elif type(current_ev).__name__ == type(hci.HCI_Ev_Cmd_Status()).__name__:
                    logging.info(f"Rd static addr status: {current_ev.status}")
                return status()

        else:
            return -100

    def parse_acl_data(self, buffer: bytes):
        packet_type, handle_pb_bc_flags, data_len = struct.unpack('<BHH',
                                                                  buffer[:5])
        handle = handle_pb_bc_flags & 0x0EFF
        pb_flag = (handle_pb_bc_flags & 0x3000) >> 12
        bc_flag = (handle_pb_bc_flags & 0xC000) >> 14

        hci_recv_acl_data_packet = hci.HCI_Recv_ACL_Data_Packet()

        if pb_flag == 0b10:
            l2cap_data = hci.HCI_Recv_L2CAP_Data()
            data = buffer[5:]
            l2cap_data.set(*struct.unpack("<HH", data[:4]), data[4:])
        else:
            l2cap_data = buffer[5:]

        hci_recv_acl_data_packet.set(
            packet_type=packet_type,
            connection_handle=handle,
            pb_flag=pb_flag,
            bc_flag=bc_flag,
            total_data_len=data_len,
            data=l2cap_data)
        return hci_recv_acl_data_packet

    def parse_subevent(self, subev_code: int):
        if subev_code == hci.HCI_SUBEV_CODE_LE_ENHANCED_CONN_CMP:
            self.hci_recv_ev_packet.current_event = \
                self.parse_subev_le_enhcd_conn_cmp(
                    self.hci_recv_ev_packet.recv_data)
            hci.events_list.append((hci.HCI_SUBEV_CODE_LE_ENHANCED_CONN_CMP,
                                    self.hci_recv_ev_packet.current_event))
            return hci.HCI_SUBEV_CODE_LE_ENHANCED_CONN_CMP

        elif subev_code == hci.HCI_SUBEV_CODE_LE_LONG_TERM_KEY_REQUEST:
            self.hci_recv_ev_packet.current_event = \
                self.parse_subev_le_long_term_key_request(
                    self.hci_recv_ev_packet.recv_data)
            hci.events_list.append(
                (hci.HCI_SUBEV_CODE_LE_LONG_TERM_KEY_REQUEST,
                 self.hci_recv_ev_packet.current_event))
            return hci.HCI_SUBEV_CODE_LE_LONG_TERM_KEY_REQUEST

        elif subev_code == hci.HCI_SUBEV_CODE_LE_DATA_LEN_CHANGE:
            self.hci_recv_ev_packet.current_event = \
                self.parse_subev_le_data_len_change(
                    self.hci_recv_ev_packet.recv_data)
            hci.events_list.append((hci.HCI_SUBEV_CODE_LE_DATA_LEN_CHANGE,
                                    self.hci_recv_ev_packet.current_event))
            return hci.HCI_SUBEV_CODE_LE_DATA_LEN_CHANGE

        elif subev_code == hci.HCI_SUBEV_CODE_LE_PHY_UPDATE_CMP:
            self.hci_recv_ev_packet.current_event = \
                self.parse_subev_le_phy_update_cmp(
                    self.hci_recv_ev_packet.recv_data)
            hci.events_list.append((hci.HCI_SUBEV_CODE_LE_PHY_UPDATE_CMP,
                                    self.hci_recv_ev_packet.current_event))
            return hci.HCI_SUBEV_CODE_LE_PHY_UPDATE_CMP

        elif subev_code == hci.HCI_SUBEV_CODE_LE_CHAN_SEL_ALG:
            self.hci_recv_ev_packet.current_event = \
                self.parse_subev_le_chan_sel_alg(
                    self.hci_recv_ev_packet.recv_data)
            hci.events_list.append((hci.HCI_SUBEV_CODE_LE_CHAN_SEL_ALG,
                                    self.hci_recv_ev_packet.current_event))
            return hci.HCI_SUBEV_CODE_LE_CHAN_SEL_ALG

        else:
            return -1

    def parse_event(self, buffer: bytes):
        self.hci_recv_ev_packet.set(*struct.unpack('<BBB', bytes(buffer[:3])),
                                    buffer[3:])
        if self.hci_recv_ev_packet.ev_code == hci.HCI_EV_CODE_DISCONN_CMP:
            self.hci_recv_ev_packet.current_event = \
                self.parse_ev_disconn_cmp(self.hci_recv_ev_packet.recv_data)
            hci.events_list.append((hci.HCI_EV_CODE_DISCONN_CMP,
                                    self.hci_recv_ev_packet.current_event))
            return hci.HCI_EV_CODE_DISCONN_CMP

        elif self.hci_recv_ev_packet.ev_code == hci.HCI_EV_CODE_CMD_CMP:
            self.hci_recv_ev_packet.current_event = \
                self.parse_ev_cmd_cmp(self.hci_recv_ev_packet.recv_data)
            hci.events_list.append((hci.HCI_EV_CODE_CMD_CMP,
                                    self.hci_recv_ev_packet.current_event))
            return hci.HCI_EV_CODE_CMD_CMP

        elif self.hci_recv_ev_packet.ev_code == hci.HCI_EV_CODE_CMD_STATUS:
            self.hci_recv_ev_packet.current_event = \
                self.parse_ev_cmd_stat(self.hci_recv_ev_packet.recv_data)
            hci.events_list.append((hci.HCI_EV_CODE_CMD_STATUS,
                                    self.hci_recv_ev_packet.current_event))
            return hci.HCI_EV_CODE_CMD_STATUS

        elif self.hci_recv_ev_packet.ev_code == hci.HCI_EV_CODE_ENCRYPTION_CHANGE:
            self.hci_recv_ev_packet.current_event = \
                self.parse_ev_encryption_change(
                    self.hci_recv_ev_packet.recv_data)
            hci.events_list.append((hci.HCI_EV_CODE_ENCRYPTION_CHANGE,
                                    self.hci_recv_ev_packet.current_event))
            return hci.HCI_EV_CODE_ENCRYPTION_CHANGE

        elif self.hci_recv_ev_packet.ev_code == hci.HCI_EV_CODE_LE_META_EVENT:
            self.hci_recv_ev_packet.current_event = \
                self.parse_ev_le_meta(self.hci_recv_ev_packet.recv_data)
            return hci.HCI_EV_CODE_LE_META_EVENT

        elif self.hci_recv_ev_packet.ev_code == hci.HCI_EV_NUM_COMP_PKTS:
            self.hci_recv_ev_packet.current_event = \
                self.parse_num_comp_pkts(self.hci_recv_ev_packet.recv_data)
            hci.events_list.append((hci.HCI_EV_NUM_COMP_PKTS,
                                    self.hci_recv_ev_packet.current_event))
            return hci.HCI_EV_NUM_COMP_PKTS

        else:
            return -1

    async def handle_event(self, buffer: bytes):
        event_code = self.parse_event(buffer)
        curr_ev = self.hci_recv_ev_packet.current_event
        if event_code == hci.HCI_EV_CODE_DISCONN_CMP:
            logging.debug("Received code: %s - HCI_EV_CODE_DISCONN_CMP",
                          event_code)
            logging.debug(
                "Status: %s for event: %s - HCI_EV_CODE_DISCONN_CMP",
                curr_ev.status,
                self.hci_recv_ev_packet.current_event)

            if curr_ev.reason == hci.CONN_FAILED_TO_BE_ESTABLISHED:
                logging.error(
                    f"Connection failed to be established. Exiting...")
                raise Exception(
                    "Connection failed to be established. Exiting...")

            if curr_ev.reason == hci.CONN_TIMEOUT:
                logging.error(f"Connection timeout. Exiting...")
                raise Exception("Connection timeout. Exiting...")

        elif event_code == hci.HCI_EV_CODE_CMD_CMP:
            logging.debug("Received code: %s - HCI_EV_CODE_CMD_CMP",
                          event_code)
            sent_opcode = self.hci_send_cmd.opcode
            recv_opcode = curr_ev.opcode

            if sent_opcode == recv_opcode:
                status = self.process_returned_parameters()
                if status != 0:
                    logging.error(
                        "Status: %s for event: %s - HCI_EV_CODE_CMD_CMP",
                        status,
                        curr_ev)
                self.async_ev_cmd_end.set()

        elif event_code == hci.HCI_EV_CODE_CMD_STATUS:
            logging.debug(
                "Received code: %s - HCI_EV_CODE_CMD_STATUS", event_code)
            sent_opcode = self.hci_send_cmd.opcode
            recv_opcode = curr_ev.opcode

            if sent_opcode == recv_opcode:
                status = self.process_returned_parameters()
                if status != 0:
                    logging.error("Status: %s for event: %s", status, curr_ev)
                self.async_ev_cmd_end.set()

        elif event_code == hci.HCI_EV_CODE_ENCRYPTION_CHANGE:
            logging.debug(
                "Received code: %s - HCI_EV_CODE_ENCRYPTION_CHANGE",
                event_code)
            status = curr_ev.status
            encryption_enabled = curr_ev.encryption_enabled
            if (status == 0 and encryption_enabled != 0):
                self.async_ev_encryption_change.set()
            else:
                raise Exception(
                    "Encryption failed. Status: %d, encryption enabled: %d",
                    status, encryption_enabled)

        elif event_code == hci.HCI_EV_CODE_LE_META_EVENT:
            logging.debug(
                "Received code: %s - HCI_EV_CODE_LE_META_EVENT", event_code)
            subev_code = self.parse_subevent(curr_ev.subevent_code)

            if subev_code == hci.HCI_SUBEV_CODE_LE_ENHANCED_CONN_CMP:
                logging.debug(
                    "Received subev code: %s - HCI_SUBEV_CODE_LE_ENHANCED_CONN_CMP",
                    subev_code)
                hci.conn_handle = self.hci_recv_ev_packet.current_event.connection_handle
                if self.async_ev_connected.is_set() == False:
                    logging.info("Connection established. Event received.")
                    self.async_ev_connected.set()

            elif subev_code == hci.HCI_SUBEV_CODE_LE_DATA_LEN_CHANGE:
                logging.debug(
                    "Received subev code: %s - HCI_SUBEV_CODE_LE_DATA_LEN_CHANGE",
                    subev_code)
                self.async_ev_set_data_len.set()

            elif subev_code == hci.HCI_SUBEV_CODE_LE_PHY_UPDATE_CMP:
                logging.debug(
                    "Received subev code: %s - HCI_SUBEV_CODE_LE_PHY_UPDATE_CMP",
                    subev_code)
                self.async_ev_update_phy.set()

            elif subev_code == hci.HCI_SUBEV_CODE_LE_CHAN_SEL_ALG:
                logging.debug(
                    "Received subev code: %s - HCI_SUBEV_CODE_LE_CHAN_SEL_ALG",
                    subev_code)

            elif subev_code == hci.HCI_SUBEV_CODE_LE_LONG_TERM_KEY_REQUEST:
                logging.debug(
                    "Received subev code: %s - HCI_SUBEV_CODE_LE_LONG_TERM_KEY_REQUEST",
                    subev_code)
                await self.cmd_le_long_term_key_request_reply(
                    hci.conn_handle, hci.ltk)

            elif subev_code < 0:
                logging.warning(f"Unknown received subevent: {buffer}\n")

        elif event_code == hci.HCI_EV_NUM_COMP_PKTS:
            logging.debug(
                "Received code: %s - HCI_EV_NUM_COMP_PKTS", event_code)
            async with self.async_lock_packets_cnt:
                hci.num_of_completed_packets_cnt += curr_ev.num_completed_packets
                hci.num_of_completed_packets_time = time.perf_counter()
            self.async_ev_num_cmp_pckts.set()

        if event_code < 0:
            logging.warning(f"Unknown received event: {buffer}\n")

        else:
            logging.debug("%s \t%s ", self.handle_event.__name__,
                          self.hci_recv_ev_packet)

    def match_recv_l2cap_data(self, buffer: bytes, timestamp: int):
        self.expected_recv_data += self.tp.predef_packet_key
        packet_key = struct.unpack("<I", buffer[-4:])[0]
        result = self.expected_recv_data == packet_key

        if result:
            self.valid_recv_data += 1

        logging.info(f"L2CAP packet number - Received: {packet_key}, \
                    Expected: {self.expected_recv_data}, Result: {result}")

        packet_number = (packet_key / self.tp.predef_packet_key) - 1

        self.tp.append_to_csv_file(timestamp, packet_number)

        # if self.tp and self.device_mode == "rx":
        #     if timestamp - self.last_timestamp > self.tp.sample_time \
        #             or packet_number == 0 \
        #             or packet_number == self.tp.total_packets_number-1:
        #         self.tp.record_throughput(packet_number, timestamp)
        #         self.last_timestamp = timestamp

        if packet_number >= self.tp.total_packets_number - 1:
            self.async_ev_recv_data_finish.set()

    def handle_acl_data(self, buffer: bytes, timestamp: int):
        hci_recv_acl_data_packet = self.parse_acl_data(buffer)
        logging.debug("%s", hci_recv_acl_data_packet)
        recv_data_type = type(hci_recv_acl_data_packet.data).__name__
        if recv_data_type == 'HCI_Recv_L2CAP_Data':
            self.match_recv_l2cap_data(buffer, timestamp)

    async def recv_handler(self):
        while not self.rx_buffer_q.empty():
            q_buffer_item, q_timestamp = self.rx_buffer_q.get()
            packet_type = struct.unpack('<B', bytes(q_buffer_item[:1]))[0]

            if packet_type == hci.HCI_ACL_DATA_PACKET:
                self.handle_acl_data(q_buffer_item, q_timestamp)

            elif packet_type == hci.HCI_EVENT_PACKET:
                await self.loop.create_task(self.handle_event(q_buffer_item))
