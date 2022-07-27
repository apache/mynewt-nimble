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

import hci_socket
import logging
import traceback
import sys


class TransportFactory:
    def __init__(self, device_index: str = None, device_mode=None,
                 asyncio_loop=None, transport_directory=None) -> None:
        if (device_index.isnumeric()):
            self.transport = hci_socket.HCI_User_Channel_Socket(
                int(device_index), device_mode, asyncio_loop)
        else:
            try:
                if (transport_directory != "default"):
                    sys.path.append(transport_directory)
                    print(sys.path)
                    import custom_transport
                    self.transport = custom_transport.Transport(device_index,
                                                                device_mode,
                                                                asyncio_loop)
                else:
                    raise Exception(
                        "Device index and transport does not match.")
            except Exception as e:
                logging.error(traceback.format_exc())
                sys.exit()

        self.rx_buffer_q = self.transport.rx_buffer_q
        self.send = self.transport.send

    def start(self):
        self.transport.start()

    def stop(self):
        self.transport.stop()
