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

import hci
import socket
import ctypes
import struct
import asyncio
import logging
import subprocess
import sys
import time
import multiprocessing


SOCKET_RECV_BUFFER_SIZE = 425984
SOCKET_RECV_TIMEOUT = 3


def btmgmt_dev_reset(index):
    logging.info(f"Selecting index {index}")
    proc = subprocess.Popen(['btmgmt', '-i', str(index), 'power', 'off'],
                            shell=False,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    proc.communicate()


class BindingError(Exception):
    pass


class HCI_User_Channel_Socket_Error(BaseException):
    pass


class HCI_User_Channel_Socket():
    def __init__(self, device_index=0, device_mode=None,
                 asyncio_loop=None):
        logging.debug(
            "Device index: %s, Device address: %s",
            device_index,
            device_mode)
        self.loop = asyncio_loop
        self.libc = ctypes.cdll.LoadLibrary('libc.so.6')
        self.rx_buffer_q = multiprocessing.Manager().Queue()
        self.counter = 0
        self.device_index = device_index
        self.device_mode = device_mode
        self.hci_socket = self.socket_create()
        self.socket_bind(self.device_index)
        self.socket_clear()
        self.listener_proc = None
        self.listener_ev = multiprocessing.Manager().Event()

    def socket_create(self):
        logging.debug("%s", self.socket_create.__name__)
        new_socket = socket.socket(socket.AF_BLUETOOTH,
                                   socket.SOCK_RAW | socket.SOCK_NONBLOCK,
                                   socket.BTPROTO_HCI)
        if new_socket is None:
            raise HCI_User_Channel_Socket_Error("Socket error. \
                                                Opening socket failed")
        new_socket.setblocking(False)
        socket_size = new_socket.getsockopt(
            socket.SOL_SOCKET, socket.SO_RCVBUF)
        logging.info(f"Default socket recv buffer size: {socket_size}")
        new_socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 500000)
        socket_size = new_socket.getsockopt(
            socket.SOL_SOCKET, socket.SO_RCVBUF)
        logging.info(f"Set socket recv buffer size: {socket_size}")
        return new_socket

    def socket_bind(self, index):
        logging.debug("%s index: %s", self.socket_bind.__name__, index)
        # addr: struct sockaddr_hci from /usr/include/bluetooth/hci.h
        addr = struct.pack(
            'HHH',
            hci.AF_BLUETOOTH,
            index,
            hci.HCI_CHANNEL_USER)
        retry_binding = 2
        for i in range(retry_binding):
            try:
                bind = self.libc.bind(self.hci_socket.fileno(),
                                      ctypes.cast(addr,
                                      ctypes.POINTER(ctypes.c_ubyte)),
                                      len(addr))
                if bind != 0:
                    raise BindingError
            except BindingError:
                logging.warning("Binding error. Trying to reset bluetooth.")
                btmgmt_dev_reset(self.device_index)
                if i < retry_binding - 1:
                    continue
                else:
                    self.hci_socket.close()
                    logging.error("Binding error. Check HCI index present.")
                    sys.exit()
            logging.info("Binding done!")
            break

    def socket_clear(self):
        logging.debug("%s", self.socket_clear.__name__)
        try:
            logging.info("Clearing the buffer...")
            time.sleep(1)
            cnt = 0
            while True:
                buff = self.hci_socket.recv(SOCKET_RECV_BUFFER_SIZE)
                cnt += len(buff)
                logging.debug(f"Read from buffer {cnt} bytes")
        except BlockingIOError:
            logging.info("Buffer empty and ready!")
            return

    async def send(self, ba_message):
        await self.loop.sock_sendall(self.hci_socket, ba_message)

    def socket_listener(self):
        recv_at_once = 0
        while True:
            try:
                if self.listener_ev.is_set():
                    logging.info("listener_ev set")
                    break
                buffer = self.hci_socket.recv(SOCKET_RECV_BUFFER_SIZE)
                logging.info(
                    f"Socket recv: {self.counter} th packet with len: {len(buffer)}")
                self.rx_buffer_q.put((buffer, time.perf_counter()))
                recv_at_once += 1
                self.counter += 1

            except BlockingIOError:
                if recv_at_once > 1:
                    logging.info(f"Socket recv in one loop: {recv_at_once}")
                    recv_at_once = 0
                pass
            except BrokenPipeError:
                logging.info("BrokenPipeError: Closing...")
                print("BrokenPipeError. Press Ctrl-C to exit...")

    def close(self):
        logging.debug("%s ", self.close.__name__)
        return self.hci_socket.close()

    def start(self):
        self.listener_proc = multiprocessing.Process(
            target=self.socket_listener, daemon=True)
        self.listener_proc.start()
        logging.info(f"start listener_proc pid: {self.listener_proc.pid}")

    def stop(self):
        logging.info(f"stop listener_proc pid: {self.listener_proc.pid}")
        self.listener_ev.set()
        self.listener_proc.join()
        self.close()
