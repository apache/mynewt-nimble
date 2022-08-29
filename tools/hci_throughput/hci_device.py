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
import hci_commands
import logging
import argparse
import sys
import asyncio
import struct
import throughput as tp
import traceback
import yaml
import util
import transport_factory
import signal

show_tp_plots = False
test_dir = None
transport_directory = None


class ParentCalledException(KeyboardInterrupt):
    """ This exception is raised when e.g. parent process sends signal.
        This allows to terminate processes correctly. """
    pass


def parse_arguments():
    parser = argparse.ArgumentParser(
        description='HCI device with User Channel Socket. \
                Start a device according to predefined mode (receiver/transmitter). \
                The initialization of the device is based on received parameters \
                or predefined init.yaml and config.yaml files.\
                The tx device will try to connect to rx device and send data. \
                After completion the throughput plots will pop up. ',
        epilog='How to run the python script: \
                    sudo python hci_device.py -m rx -oa 00:00:00:00:00:00 -oat 0 -di 0 \
                        -pa 00:00:00:00:00:00 -pat 0 -pdi 0 -cf config.yaml\
                or, if present, specifying init.yaml file \
                    sudo python main.py -m tx -if init.yaml')
    parser.add_argument('-m', '--mode', type=str, nargs="?",
                        help='device mode - receiver, transmitter',
                        choices=['rx', 'tx'])
    parser.add_argument('-if', '--init_file', type=str, nargs="?",
                        help='yaml init file, e.g.: -f init.yaml',
                        default="init.yaml")
    parser.add_argument('-oa', '--own_addr', type=str, nargs="?",
                        help='device own address, e.g.: -oa 00:00:00:00:00:00')
    parser.add_argument('-oat', '--own_addr_type', type=int, nargs="?",
                        help='device own address type, public e.g.: -oat 0')
    parser.add_argument('-di', '--dev_idx', type=str, nargs="?",
                        help='device own hci index, hci0 e.g.: -ohi 0')
    parser.add_argument(
        '-pa', '--peer_addr', type=str, nargs="?",
        help='peer device address, e.g.: -pa 00:00:00:00:00:00')
    parser.add_argument(
        '-pat', '--peer_addr_type', type=int, nargs="?",
        help='peer device own address type, public e.g.: -pat 0')
    parser.add_argument('-pdi', '--peer_dev_idx', type=str, nargs="?",
                        help='peer device index, e.g. hci0: -phi 0')
    parser.add_argument('-cf', '--config_file', type=str, nargs="?",
                        help='yaml config file, e.g.: -f config.yaml',
                        default="config.yaml")

    try:
        args = parser.parse_args()
        return args

    except Exception as e:
        logging.error(traceback.format_exc())
        sys.exit()


async def set_phy(bt_dev: hci_commands.HCI_Commands, conn_handle, cfg_phy,
                  supported_features):
    def error(info):
        print("ERROR: Check log files")
        raise Exception(info, ": Unsupported PHY. Closing...")

    PHY_2M = supported_features & hci.LE_FEATURE_2M_PHY
    PHY_CODED = supported_features & hci.LE_FEATURE_CODED_PHY

    if (cfg_phy == "1M"):
        await bt_dev.cmd_le_set_phy(conn_handle, all_phys=0, tx_phys=1, rx_phys=1, phy_options=0)
        logging.info(f"PHY 1M")

    elif (cfg_phy == "2M"):
        if (PHY_2M):
            await bt_dev.cmd_le_set_phy(conn_handle, all_phys=0, tx_phys=2, rx_phys=2, phy_options=0)
            logging.info(f"PHY 2M")
        else:
            error("2M")

    elif (cfg_phy == "Coded"):
        if (PHY_CODED):
            await bt_dev.cmd_le_set_phy(conn_handle, all_phys=0, tx_phys=3, rx_phys=3, phy_options=0)
            logging.info(f"PHY Coded")
        else:
            error("Coded")

    else:
        error("Possible PHY in config.yaml: 1M, 2M, Coded")


async def init(bt_dev: hci_commands.HCI_Commands, ini: dict, cfg: dict):
    """ init: Assumed to be the same for all devices """
    asyncio.create_task(bt_dev.rx_buffer_q_wait())
    await bt_dev.cmd_reset()
    if ini["own_address_type"]:
        await bt_dev.cmd_le_set_random_addr(ini["own_address"])
    await bt_dev.cmd_set_event_mask(mask=0x200080000204e090)
    await bt_dev.cmd_le_set_event_mask(mask=0x00000007FFFFFFFF)
    await bt_dev.cmd_le_read_local_supported_features()
    await bt_dev.cmd_le_read_buffer_size()
    await bt_dev.cmd_le_read_max_data_len()


async def finish(bt_dev: hci_commands.HCI_Commands, cfg: dict):
    logging.info("Received %s good packets", bt_dev.valid_recv_data)
    if bt_dev.tp:
        if show_tp_plots:
            bt_dev.tp.plot_tp_from_file(sample_time=cfg["tp"]["sample_time"])
        if bt_dev.device_mode == "rx":
            bt_dev.tp.save_average()
            util.copy_log_files_to_test_directory(test_dir)
    logging.info(f"Correctly received: {bt_dev.valid_recv_data}")
    logging.info(f"Sent packets: {bt_dev.sent_packets_counter}")
    bt_dev.async_ev_rx_wait_finish.set()
    # Wait for rx_buffer_q_wait task to finish and socket to close
    await asyncio.sleep(1)


async def async_main_rx(bt_dev: hci_commands.HCI_Commands, ini: dict, cfg: dict):
    await init(bt_dev, ini, cfg)

    bt_dev.tp = tp.Throughput(name="tp_receiver", mode=bt_dev.device_mode,
                              total_packets_number=hci.num_of_packets_to_send,
                              bytes_number_in_packet=hci.num_of_bytes_to_send,
                              throughput_data_type=cfg["tp"]["data_type"],
                              flag_plot_packets=cfg["tp"]["flag_plot_packets"],
                              sample_time=cfg["tp"]["sample_time"],
                              test_directory=test_dir)
    ############
    # ADVERTISE
    ############
    adv_params = hci.HCI_Advertising()
    adv_params.set(
        advertising_interval_min=cfg["adv"]["advertising_interval_min"],
        advertising_interval_max=cfg["adv"]["advertising_interval_max"],
        advertising_type=cfg["adv"]["advertising_type"],
        own_address_type=ini["own_address_type"],
        peer_address_type=ini["peer_address_type"],
        peer_address=cfg["adv"]["peer_address"],
        advertising_channel_map=cfg["adv"]["advertising_channel_map"],
        advertising_filter_policy=cfg["adv"]["advertising_filter_policy"]
    )
    await bt_dev.cmd_le_set_advertising_params(adv_params)
    await bt_dev.cmd_le_set_advertising_enable(1)

    await hci_commands.wait_for_event(bt_dev.async_ev_connected, hci.WAIT_FOR_EVENT_CONN_TIMEOUT)

    await bt_dev.cmd_le_set_data_len(hci.conn_handle, tx_octets=0, tx_time=0)
    await hci_commands.wait_for_event(bt_dev.async_ev_set_data_len, hci.WAIT_FOR_EVENT_TIMEOUT)

    logging.debug("Before finish event")
    await asyncio.shield(bt_dev.async_ev_recv_data_finish.wait())
    logging.debug("after finish event")
    bt_dev.async_ev_recv_data_finish.clear()

    await bt_dev.cmd_le_set_advertising_enable(0)

    await finish(bt_dev, cfg)


async def async_main_tx(bt_dev: hci_commands.HCI_Commands, ini: dict, cfg: dict):
    await init(bt_dev, ini, cfg)

    conn_params = hci.HCI_Connect()
    conn_params.set(
        le_scan_interval=cfg["conn"]["le_scan_interval"],
        le_scan_window=cfg["conn"]["le_scan_window"],
        initiator_filter_policy=cfg["conn"]["initiator_filter_policy"],
        peer_address_type=ini['peer_address_type'],
        peer_address=ini['peer_address'],
        own_address_type=ini['own_address_type'],
        connection_interval_min=cfg["conn"]["connection_interval_min"],
        connection_interval_max=cfg["conn"]["connection_interval_max"],
        max_latency=cfg["conn"]["max_latency"],
        supervision_timeout=cfg["conn"]["supervision_timeout"],
        min_ce_length=cfg["conn"]["min_ce_length"],
        max_ce_length=cfg["conn"]["max_ce_length"]
    )

    await bt_dev.cmd_le_create_connection(conn_params)
    await hci_commands.wait_for_event(bt_dev.async_ev_connected, hci.WAIT_FOR_EVENT_CONN_TIMEOUT)

    await bt_dev.cmd_le_set_data_len(hci.conn_handle, tx_octets=0, tx_time=0)
    await hci_commands.wait_for_event(bt_dev.async_ev_set_data_len, hci.WAIT_FOR_EVENT_TIMEOUT)

    await set_phy(bt_dev, hci.conn_handle, cfg['phy'],
                  hci.le_read_local_supported_features.le_features)
    await hci_commands.wait_for_event(bt_dev.async_ev_update_phy, hci.WAIT_FOR_EVENT_TIMEOUT)

    if cfg["enable_encryption"]:
        await bt_dev.cmd_le_enable_encryption(hci.conn_handle, random_number=0, ediv=0, ltk=hci.ltk)
        await hci_commands.wait_for_event(bt_dev.async_ev_encryption_change, 10)

    ############
    # L2CAP SEND
    ############
    l2cap_data = hci.L2CAP_Data_Send()
    acl_data = hci.HCI_ACL_Data_Send()

    packets_to_send = hci.num_of_packets_to_send
    packet_credits = hci.le_read_buffer_size.total_num_le_acl_data_packets
    fmt = "<" + str(hci.num_of_bytes_to_send) + "B"
    data = struct.pack(fmt, *([0] * hci.num_of_bytes_to_send))
    last_value = 0
    sent_packets = 0
    tx_sent_timestamps = []

    bt_dev.tp = tp.Throughput(name="tp_transmitter", mode=bt_dev.device_mode,
                              total_packets_number=hci.num_of_packets_to_send,
                              bytes_number_in_packet=hci.num_of_bytes_to_send,
                              throughput_data_type=cfg["tp"]["data_type"],
                              flag_plot_packets=cfg["tp"]["flag_plot_packets"],
                              sample_time=cfg["tp"]["sample_time"],
                              test_directory=test_dir)

    async with bt_dev.async_lock_packets_cnt:
        hci.num_of_completed_packets_cnt = 0

    while sent_packets < hci.num_of_packets_to_send:
        if packet_credits > 0 and packets_to_send > 0:
            data, last_value = tp.gen_data(
                hci.num_of_bytes_to_send, last_value)
            l2cap_data.set(channel_id=0x0044, data=data)
            acl_data.set(connection_handle=hci.conn_handle, pb_flag=0b00,
                         bc_flag=0b00, data=l2cap_data.ba_full_message)
            await bt_dev.acl_data_send(acl_data)
            async with bt_dev.async_lock_packets_cnt:
                packets_to_send -= 1
                packet_credits -= 1
        else:
            logging.info(f"Waiting for num_of_cmp_packets event")
            await bt_dev.async_ev_num_cmp_pckts.wait()
            bt_dev.async_ev_num_cmp_pckts.clear()

        if hci.num_of_completed_packets_cnt > 0:
            async with bt_dev.async_lock_packets_cnt:
                sent_packets += hci.num_of_completed_packets_cnt
                tx_sent_timestamps.append((hci.num_of_completed_packets_time,
                                           sent_packets))
                logging.info(f"Sent : {sent_packets}")

                packet_credits += hci.num_of_completed_packets_cnt
                hci.num_of_completed_packets_cnt = 0

    for timestamp in tx_sent_timestamps:
        bt_dev.tp.append_to_csv_file(*timestamp)

    await finish(bt_dev, cfg)


def parse_cfg_files(args) -> dict:
    if args.init_file is None:
        ini = {
            "own_address": args.own_addr,
            "own_address_type": args.own_addr_type,
            "dev_index": args.dev_idx,
            "peer_address": args.peer_addr,
            "peer_address_type": args.peer_addr_type,
            "peer_dev_index": args.peer_dev_idx
        }
    else:
        with open(args.init_file, "r") as file:
            init_file = yaml.safe_load(file)
        ini = init_file[args.mode]
        global test_dir, transport_directory, ltk
        test_dir = init_file["test_dir"]
        transport_directory = init_file["transport_directory"]
        hci.ltk = int(init_file["ltk"], 16)

    with open(args.config_file) as f:
        cfg = yaml.safe_load(f)

    global show_tp_plots

    hci.num_of_bytes_to_send = cfg["num_of_bytes_to_send"]
    hci.num_of_packets_to_send = cfg["num_of_packets_to_send"]
    show_tp_plots = cfg["show_tp_plots"]

    return ini, cfg


def signal_handler(signum, frame):
    logging.critical(f"Received signal: {signal.Signals(signum).name}")
    raise ParentCalledException(
        f"Received signal: {signal.Signals(signum).name}")


def main():
    args = parse_arguments()
    ini, cfg = parse_cfg_files(args)
    log_path = f"log/log_{args.mode}.log"
    transport = None

    try:
        util.configure_logging(log_path, clear_log_file=True)

        loop = asyncio.get_event_loop()
        loop.set_debug(True)

        transport = transport_factory.TransportFactory(
            device_index=ini['dev_index'],
            device_mode=args.mode, asyncio_loop=loop,
            transport_directory=transport_directory)

        signal.signal(signal.SIGTERM, signal_handler)

        bt_dev = hci_commands.HCI_Commands(send=transport.send,
                                           rx_buffer_q=transport.rx_buffer_q,
                                           asyncio_loop=loop,
                                           device_mode=args.mode)

        transport.start()

        if args.mode == 'rx':
            loop.run_until_complete(async_main_rx(bt_dev, ini, cfg))
        elif args.mode == 'tx':
            loop.run_until_complete(async_main_tx(bt_dev, ini, cfg))

    except Exception as e:
        logging.error(traceback.format_exc())
    except (KeyboardInterrupt or ParentCalledException):
        logging.critical("Hard exit triggered.")
        logging.error(traceback.format_exc())
    finally:
        if transport is not None:
            transport.stop()
        sys.exit()


if __name__ == '__main__':
    main()
