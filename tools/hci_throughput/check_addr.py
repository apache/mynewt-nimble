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

import argparse
import asyncio
import hci_commands
import sys
import logging
import hci
import traceback
import util
import transport_factory


def parse_arguments():
    parser = argparse.ArgumentParser(
        description='Check HCI device address type and address',
        epilog='How to run script: \
                sudo python check_addr.py -i 0 1 2 \
                -t path/to/custom_transport_dir')
    parser.add_argument('-i', '--indexes', type=str, nargs='*',
                        help='specify hci adapters indexes', default=0)
    parser.add_argument('-t', '--transport_directory', type=str, nargs='*',
                        help='specify hci transport directory path. \
                        Use for transport other than the default linux socket.',
                        default=["default"])
    try:
        args = parser.parse_args()
        if (isinstance(args.transport_directory, list)):
            args.transport_directory = args.transport_directory.pop()
        else:
            args.transport_directory = args.transport_directory

    except Exception as e:
        print(traceback.format_exc())
    return args


async def main(dev: hci_commands.HCI_Commands):
    result = tuple()
    task = asyncio.create_task(dev.rx_buffer_q_wait())
    await dev.cmd_reset()
    await dev.cmd_read_bd_addr()

    if hci.bdaddr != '00:00:00:00:00:00':
        logging.info("Type public: %s, address: %s",
                     hci.PUBLIC_ADDRESS_TYPE, hci.bdaddr)
        result = (0, hci.bdaddr)
        print("Public address: ", result)
    else:
        await dev.cmd_vs_read_static_addr()
        if hci.static_addr != '00:00:00:00:00:00':
            logging.info("Type static random: %s, address: %s",
                         hci.STATIC_RANDOM_ADDRESS_TYPE, hci.static_addr)
            result = (1, hci.static_addr)
            print("Static random address: ", result)
        else:
            addr = hci.gen_static_rand_addr()
            logging.info("Type static random: %s, generated address: %s",
                         hci.STATIC_RANDOM_ADDRESS_TYPE, addr)
            result = (1, addr)
            print("Generated static random address: ", result)
    task.cancel()
    return result


def check_addr(
        device_indexes: list,
        addresses: list,
        transport_directory: str) -> list:
    util.configure_logging(f"log/check_addr.log", clear_log_file=True)

    logging.info(f"Devices indexes: {device_indexes}")
    for index in device_indexes:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        loop.set_debug(True)

        transport = transport_factory.TransportFactory(device_index=str(
            index), asyncio_loop=loop, transport_directory=transport_directory)

        bt_dev = hci_commands.HCI_Commands(send=transport.send,
                                           rx_buffer_q=transport.rx_buffer_q,
                                           asyncio_loop=loop)

        transport.start()

        addresses.append(loop.run_until_complete(main(bt_dev)))

        transport.stop()
        loop.close()

    logging.info(f"Finished: {addresses}")
    return addresses


if __name__ == '__main__':
    try:
        args = parse_arguments()
        print(args)
        addresses = []
        addresses = check_addr(args.indexes, addresses,
                               args.transport_directory)
        print(addresses)
    except Exception as e:
        print(traceback.format_exc())
    finally:
        sys.exit()
