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

from dataclasses import dataclass
import struct
from binascii import unhexlify
import random
import ctypes

############
# DEFINES
############
AF_BLUETOOTH = 31
HCI_CHANNEL_USER = 1
HCI_COMMAND_PACKET = 0x01
HCI_ACL_DATA_PACKET = 0x02
HCI_EVENT_PACKET = 0x04

L2CAP_HDR_BYTES = 4

HCI_EV_CODE_DISCONN_CMP = 0x05
HCI_EV_CODE_ENCRYPTION_CHANGE = 0x08
HCI_EV_CODE_CMD_CMP = 0x0e
HCI_EV_CODE_CMD_STATUS = 0x0f
HCI_EV_CODE_LE_META_EVENT = 0x3e
HCI_SUBEV_CODE_LE_ENHANCED_CONN_CMP = 0x0a
HCI_SUBEV_CODE_LE_DATA_LEN_CHANGE = 0x07
HCI_SUBEV_CODE_LE_PHY_UPDATE_CMP = 0x0c
HCI_SUBEV_CODE_LE_CHAN_SEL_ALG = 0x14
HCI_SUBEV_CODE_LE_LONG_TERM_KEY_REQUEST = 0x05
HCI_EV_NUM_COMP_PKTS = 0x13

CONN_FAILED_TO_BE_ESTABLISHED = 0x3e
CONN_TIMEOUT = 0x08

OGF_HOST_CTL = 0x03
OCF_SET_EVENT_MASK = 0x0001
OCF_RESET = 0X0003

OGF_INFO_PARAM = 0x04
OCF_READ_LOCAL_COMMANDS = 0x0002
OCF_READ_BD_ADDR = 0x0009

OGF_LE_CTL = 0x08
OCF_LE_SET_EVENT_MASK = 0x0001
OCF_LE_READ_BUFFER_SIZE_V1 = 0x0002
OCF_LE_READ_LOCAL_SUPPORTED_FEATURES = 0x0003
OCF_LE_READ_BUFFER_SIZE_V2 = 0x0060
OCF_LE_SET_RANDOM_ADDRESS = 0x0005
OCF_LE_SET_ADVERTISING_PARAMETERS = 0x0006
OCF_LE_SET_ADVERTISE_ENABLE = 0x000a
OCF_LE_SET_SCAN_PARAMETERS = 0x000b
OCF_LE_SET_SCAN_ENABLE = 0x000c
OCF_LE_CREATE_CONN = 0x000d
OCF_LE_ENABLE_ENCRYPTION = 0x0019
OCF_LE_LONG_TERM_KEY_REQUEST_REPLY = 0x001A
OCF_LE_SET_DATA_LEN = 0x0022
OCF_LE_READ_SUGGESTED_DFLT_DATA_LEN = 0x0023
OCF_LE_READ_MAX_DATA_LEN = 0x002f
OCF_LE_READ_PHY = 0x0030
OCF_LE_SET_DFLT_PHY = 0x0031
OCF_LE_SET_PHY = 0x0032

OGF_VENDOR_SPECIFIC = 0x003f
BLE_HCI_OCF_VS_RD_STATIC_ADDR = 0x0001

PUBLIC_ADDRESS_TYPE = 0
STATIC_RANDOM_ADDRESS_TYPE = 1

WAIT_FOR_EVENT_TIMEOUT = 5
WAIT_FOR_EVENT_CONN_TIMEOUT = 25

LE_FEATURE_2M_PHY = ctypes.c_uint64(0x0100).value
LE_FEATURE_CODED_PHY = ctypes.c_uint64(0x0800).value

############
# GLOBAL VAR
############
num_of_bytes_to_send = None  # based on supported_max_tx_octets
num_of_packets_to_send = None

events_list = []
bdaddr = '00:00:00:00:00:00'
static_addr = '00:00:00:00:00:00'
le_read_buffer_size = None
conn_handle = 0
requested_tx_octets = 1
requested_tx_time = 1
suggested_dflt_data_len = None
max_data_len = None
phy = None
ev_num_comp_pkts = None
num_of_completed_packets_cnt = 0
num_of_completed_packets_time = 0
read_local_commands = None
le_read_local_supported_features = None
ltk = None

############
# FUNCTIONS
############


def get_opcode(ogf: int, ocf: int):
    return ((ocf & 0x03ff) | (ogf << 10))


def get_ogf_ocf(opcode: int):
    ogf = opcode >> 10
    ocf = opcode & 0x03ff
    return ogf, ocf


def cmd_addr_to_ba(addr_str: str):
    return unhexlify("".join(addr_str.split(':')))[::-1]


def ba_addr_to_str(addr_ba: bytearray):
    addr_str = addr_ba.hex().upper()
    return ':'.join(addr_str[i:i + 2]
                    for i in range(len(addr_str), -2, -2))[1:]


def gen_static_rand_addr():
    while True:
        x = [random.randint(0, 1) for _ in range(0, 48)]

        if 0 in x[:-2] and 1 in x[:-2]:
            x[0] = 1
            x[1] = 1
            break
    addr_int = int("".join([str(x[i]) for i in range(0, len(x))]), 2)
    addr_hex = "{0:0{1}x}".format(addr_int, 12)
    addr = ":".join(addr_hex[i:i + 2] for i in range(0, len(addr_hex), 2))
    return addr.upper()

############
# GLOBAL VAR CLASSES
############


@dataclass
class Suggested_Dflt_Data_Length():
    status: int
    suggested_max_tx_octets: int
    suggested_max_tx_time: int

    def __init__(self):
        self.set()

    def set(
            self,
            status=0,
            suggested_max_tx_octets=0,
            suggested_max_tx_time=0):
        self.status = status
        self.suggested_max_tx_octets = suggested_max_tx_octets
        self.suggested_max_tx_time = suggested_max_tx_time


@dataclass
class Max_Data_Length():
    status: int
    supported_max_tx_octets: int
    supported_max_tx_time: int
    supported_max_rx_octets: int
    supported_max_rx_time: int

    def __init__(self):
        self.set()

    def set(self, status=0, supported_max_tx_octets=0, supported_max_tx_time=0,
            supported_max_rx_octets=0, supported_max_rx_time=0):
        self.status = status
        self.supported_max_tx_octets = supported_max_tx_octets
        self.supported_max_tx_time = supported_max_tx_time
        self.supported_max_rx_octets = supported_max_rx_octets
        self.supported_max_rx_time = supported_max_rx_time


@dataclass
class LE_Read_Buffer_Size:
    status: int
    le_acl_data_packet_length: int
    total_num_le_acl_data_packets: int
    iso_data_packet_len: int
    total_num_iso_data_packets: int

    def __init__(self):
        self.set()

    def set(self, status=0, le_acl_data_packet_length=0,
            total_num_le_acl_data_packets=0, iso_data_packet_len=0,
            total_num_iso_data_packets=0):
        self.status = status
        self.le_acl_data_packet_length = le_acl_data_packet_length
        self.total_num_le_acl_data_packets = total_num_le_acl_data_packets
        self.iso_data_packet_len = iso_data_packet_len
        self.total_num_iso_data_packets = total_num_iso_data_packets


@dataclass
class LE_Read_PHY:
    status: int
    connection_handle: int
    tx_phy: int
    rx_phy: int

    def __init__(self):
        self.set()

    def set(self, status=0, connection_handle=0, tx_phy=0, rx_phy=0):
        self.status = status
        self.connection_handle = connection_handle
        self.tx_phy = tx_phy
        self.rx_phy = rx_phy


@dataclass
class Read_Local_Commands:
    status: int
    supported_commands: bytes

    def __init__(self):
        self.set()

    def set(self, rcv_bytes=bytes(65)):
        self.status = int(rcv_bytes[0])
        self.supported_commands = rcv_bytes[1:]


@dataclass
class LE_Read_Local_Supported_Features:
    status: int
    le_features: bytes

    def __init__(self):
        self.set()

    def set(self, rcv_bytes=bytes(9)):
        self.status = int(rcv_bytes[0])
        self.le_features = ctypes.c_uint64.from_buffer_copy(
            rcv_bytes[1:]).value

############
# EVENTS
############


@dataclass
class HCI_Ev_Disconn_Complete:
    status: int
    connection_handle: int
    reason: int

    def __init__(self):
        self.set()

    def set(self, status=0, connection_handle=0, reason=0):
        self.status = status
        self.connection_handle = connection_handle
        self.reason = reason


@dataclass
class HCI_Ev_Cmd_Complete:
    num_hci_command_packets: int
    opcode: int
    return_parameters: int

    def __init__(self):
        self.set()

    def set(self, num_hci_cmd_packets=0, opcode=0, return_parameters=b''):
        self.num_hci_command_packets = num_hci_cmd_packets
        self.opcode = opcode
        self.return_parameters = return_parameters


@dataclass
class HCI_Ev_Cmd_Status:
    status: int
    num_hci_command_packets: int
    opcode: int

    def __init__(self):
        self.set()

    def set(self, status=0, num_hci_cmd_packets=0, opcode=0):
        self.status = status
        self.num_hci_command_packets = num_hci_cmd_packets
        self.opcode = opcode


@dataclass
class HCI_Ev_LE_Encryption_Change():
    status: int
    connection_handle: int
    encryption_enabled: int

    def __init__(self):
        self.set()

    def set(self, status=0, connection_handle=0, encryption_enabled=0):
        self.status = status
        self.connection_handle = connection_handle
        self.encryption_enabled = encryption_enabled


@dataclass
class HCI_Ev_LE_Meta:
    subevent_code: int

    def __init__(self):
        self.set()

    def set(self, subevent_code=0):
        self.subevent_code = subevent_code


@dataclass
class HCI_Ev_LE_Enhanced_Connection_Complete(HCI_Ev_LE_Meta):
    status: int
    connection_handle: int
    role: int
    peer_address_type: int
    peer_address: str
    local_resolvable_private_address: int
    peer_resolvable_private_address: int
    connection_interval: int
    peripheral_latency: int
    supervision_timeout: int
    central_clock_accuracy: int

    def __init__(self):
        self.set()

    def set(self, subevent_code=0, status=0, connection_handle=0, role=0,
            peer_address_type=0, peer_address='00:00:00:00:00:00',
            local_resolvable_private_address='00:00:00:00:00:00',
            peer_resolvable_private_address='00:00:00:00:00:00',
            connection_interval=0, peripheral_latency=0, supervision_timeout=0,
            central_clock_accuracy=0):
        super().set(subevent_code)
        self.status = status
        self.connection_handle = connection_handle
        self.role = role
        self.peer_address_type = peer_address_type
        self.peer_address = peer_address
        self.local_resolvable_private_address = local_resolvable_private_address
        self.peer_resolvable_private_address = peer_resolvable_private_address
        self.connection_interval = connection_interval
        self.peripheral_latency = peripheral_latency
        self.supervision_timeout = supervision_timeout
        self.central_clock_accuracy = central_clock_accuracy


@dataclass
class HCI_Ev_LE_Data_Length_Change(HCI_Ev_LE_Meta):
    conn_handle: int
    max_tx_octets: int
    max_tx_time: int
    max_rx_octets: int
    max_rx_time: int
    triggered: int

    def __init__(self):
        self.set()

    def set(self, subevent_code=0, conn_handle=0, max_tx_octets=0,
            max_tx_time=0, max_rx_octets=0, max_rx_time=0, triggered=0):
        super().set(subevent_code)
        self.conn_handle = conn_handle
        self.max_tx_octets = max_tx_octets
        self.max_tx_time = max_tx_time
        self.max_rx_octets = max_rx_octets
        self.max_rx_time = max_rx_time
        self.triggered = triggered


@dataclass
class HCI_Ev_LE_Long_Term_Key_Request(HCI_Ev_LE_Meta):
    conn_handle: int
    random_number: int
    encrypted_diversifier: int

    def __init__(self):
        self.set()

    def set(self, subevent_code=0, conn_handle=0, random_number=0,
            encrypted_diversifier=0):
        super().set(subevent_code)
        self.conn_handle = conn_handle
        self.random_number = random_number
        self.encrypted_diversifier = encrypted_diversifier


@dataclass
class HCI_Ev_LE_PHY_Update_Complete(HCI_Ev_LE_Meta):
    status: int
    connection_handle: int
    tx_phy: int
    rx_phy: int

    def __init__(self):
        self.set()

    def set(self, subevent_code=0, status=0, connection_handle=0,
            tx_phy=0, rx_phy=0):
        super().set(subevent_code)
        self.status = status
        self.connection_handle = connection_handle
        self.tx_phy = tx_phy
        self.rx_phy = rx_phy


@dataclass
class HCI_Number_Of_Completed_Packets:
    num_handles: int
    connection_handle: int
    num_completed_packets: int

    def __init__(self):
        self.set()

    def set(self, num_handles=0, connection_handle=0, num_completed_packets=0):
        self.num_handles = num_handles
        self.connection_handle = connection_handle
        self.num_completed_packets = num_completed_packets


class HCI_Ev_LE_Chan_Sel_Alg(HCI_Ev_LE_Meta):
    connection_handle: int
    algorithm: int

    def __init__(self):
        self.set()

    def set(self, subevent_code=0, connection_handle=0, algorithm=0):
        super().set(subevent_code)
        self.connection_handle = connection_handle
        self.algorithm = algorithm

############
# PARAMETERS
############


@dataclass
class HCI_Advertising:
    advertising_interval_min: int
    advertising_interval_max: int
    advertising_type: int
    own_address_type: int
    peer_address_type: int
    peer_address: str
    advertising_channel_map: int
    advertising_filter_policy: int
    ba_full_message: bytearray

    def __init__(self):
        self.set()

    def set(self, advertising_interval_min=0, advertising_interval_max=0,
            advertising_type=0, own_address_type=0, peer_address_type=0,
            peer_address='00:00:00:00:00:00', advertising_channel_map=0,
            advertising_filter_policy=0):
        self.advertising_interval_min = advertising_interval_min
        self.advertising_interval_max = advertising_interval_max
        self.advertising_type = advertising_type
        self.own_address_type = own_address_type
        self.peer_address_type = peer_address_type
        self.peer_address = peer_address
        self.advertising_channel_map = advertising_channel_map
        self.advertising_filter_policy = advertising_filter_policy
        self.ba_full_message = bytearray(
            struct.pack(
                '<HHBBBBB',
                advertising_interval_min,
                advertising_interval_max,
                advertising_type,
                own_address_type,
                peer_address_type,
                advertising_channel_map,
                advertising_filter_policy))
        peer_addr_ba = cmd_addr_to_ba(peer_address)
        self.ba_full_message[7:7] = peer_addr_ba


@dataclass
class HCI_Scan:
    le_scan_type: int
    le_scan_interval: int
    le_scan_window: int
    own_address_type: int
    scanning_filter_policy: int
    ba_full_message: bytearray

    def __init__(self):
        self.set()

    def set(self, le_scan_type=0, le_scan_interval=0, le_scan_window=0,
            own_address_type=0, scanning_filter_policy=0):
        self.le_scan_type = le_scan_type
        self.le_scan_interval = le_scan_interval
        self.le_scan_window = le_scan_window
        self.own_address_type = own_address_type
        self.scanning_filter_policy = scanning_filter_policy
        self.ba_full_message = bytearray(
            struct.pack(
                '<BHHBB',
                le_scan_type,
                le_scan_interval,
                le_scan_window,
                own_address_type,
                scanning_filter_policy))


@dataclass
class HCI_Connect:
    le_scan_interval: int
    le_scan_window: int
    initiator_filter_policy: int
    peer_address_type: int
    peer_address: str
    own_address_type: int
    connection_interval_min: int
    connection_interval_max: int
    max_latency: int
    supervision_timeout: int
    min_ce_length: int
    max_ce_length: int
    ba_full_message: bytearray

    def __init__(self):
        self.set()

    def set(self, le_scan_interval=0, le_scan_window=0,
            initiator_filter_policy=0, peer_address_type=0,
            peer_address='00:00:00:00:00:00', own_address_type=0,
            connection_interval_min=0, connection_interval_max=0,
            max_latency=0, supervision_timeout=0, min_ce_length=0,
            max_ce_length=0):
        self.le_scan_interval = le_scan_interval
        self.le_scan_window = le_scan_window
        self.initiator_filter_policy = initiator_filter_policy
        self.peer_address_type = peer_address_type
        self.peer_address = peer_address
        self.own_address_type = own_address_type
        self.connection_interval_min = connection_interval_min
        self.connection_interval_max = connection_interval_max
        self.max_latency = max_latency
        self.supervision_timeout = supervision_timeout
        self.min_ce_length = min_ce_length
        self.max_ce_length = max_ce_length
        self.ba_full_message = bytearray(
            struct.pack(
                '<HHBBBHHHHHH',
                le_scan_interval,
                le_scan_window,
                initiator_filter_policy,
                peer_address_type,
                own_address_type,
                connection_interval_min,
                connection_interval_max,
                max_latency,
                supervision_timeout,
                min_ce_length,
                max_ce_length))
        peer_addr_ba = cmd_addr_to_ba(peer_address)
        self.ba_full_message[6:6] = peer_addr_ba

############
# RX / TX
############


@dataclass
class HCI_Receive:
    packet_type: int

    def __init__(self):
        self.set()

    def set(self, packet_type=0):
        self.packet_type = packet_type


@dataclass
class HCI_Recv_Event_Packet(HCI_Receive):
    ev_code: int
    packet_len: int
    recv_data: bytearray
    current_event: None

    def __init__(self):
        self.set()

    def set(self, packet_type=0, ev_code=0, packet_len=0,
            recv_data=bytearray(256)):
        super().set(packet_type)
        self.ev_code = ev_code
        self.packet_len = packet_len
        self.recv_data = recv_data
        self.recv_data = recv_data[:packet_len]


@dataclass
class HCI_Recv_ACL_Data_Packet(HCI_Receive):
    connection_handle: int
    pb_flag: int
    bc_flag: int
    data_total_len: int
    data: bytearray

    def __init__(self):
        self.set()

    def set(self, packet_type=0, connection_handle=0,
            pb_flag=0, bc_flag=0, total_data_len=0, data=b''):
        super().set(packet_type)
        self.connection_handle = connection_handle
        self.pb_flag = pb_flag
        self.bc_flag = bc_flag
        self.data_total_len = total_data_len
        self.data = data


@dataclass
class HCI_Recv_L2CAP_Data:
    pdu_length: int
    channel_id: int
    data: bytearray

    def __init__(self):
        self.set()

    def set(self, pdu_length=0, channel_id=0, data=b''):
        self.pdu_length = pdu_length
        self.channel_id = channel_id
        self.data = data


@dataclass
class HCI_Cmd_Send:
    packet_type: int
    ogf: int
    ocf: int
    packet_len: int
    data: bytearray
    ba_full_message: bytearray

    def __init__(self):
        self.set()

    def set(self, ogf=0, ocf=0, data=b''):
        self.packet_type = HCI_COMMAND_PACKET
        self.ogf = ogf
        self.ocf = ocf
        self.opcode = get_opcode(ogf, ocf)
        self.packet_len = len(data)
        self.data = data
        self.ba_full_message = bytearray(
            struct.pack(
                '<BHB',
                self.packet_type,
                self.opcode,
                self.packet_len))
        self.ba_full_message.extend(self.data)


@dataclass
class HCI_ACL_Data_Send:
    packet_type: int
    connection_handle: int
    pb_flag: int
    bc_flag: int
    data_total_length: int
    data: bytearray
    ba_full_message: bytearray

    def __init__(self):
        self.set()

    def set(self, connection_handle=0, pb_flag=0b00, bc_flag=0b00, data=b''):
        self.packet_type = HCI_ACL_DATA_PACKET
        self.connection_handle = connection_handle
        self.pb_flag = pb_flag
        self.bc_flag = bc_flag
        self.data_total_length = len(data)
        self.data = data
        self.ba_full_message = bytearray(
            struct.pack(
                '<BHH',
                self.packet_type,
                ((self.connection_handle & 0x0eff) | (
                    self.pb_flag << 12) | (
                    self.bc_flag << 14)),
                self.data_total_length))
        self.ba_full_message.extend(self.data)


@dataclass
class L2CAP_Data_Send:
    pdu_length: int
    channel_id: int
    data: bytearray
    ba_full_message: bytearray

    def __init__(self):
        self.set()

    def set(self, pdu_length=0, channel_id=0, data=b''):
        if not pdu_length:
            self.pdu_length = len(data)
        else:
            self.pdu_length = pdu_length
        self.channel_id = channel_id
        self.data = data
        fmt_conf = "<HH"
        self.ba_full_message = bytearray(
            struct.pack(
                fmt_conf,
                self.pdu_length,
                self.channel_id))
        self.ba_full_message.extend(data)
