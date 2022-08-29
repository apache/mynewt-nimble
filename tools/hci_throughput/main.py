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

import multiprocessing
import check_addr
import argparse
import yaml
import sys
import subprocess
import traceback
import matplotlib.pyplot as plt
import csv
import util
import os
import math
import random

PROCESS_TIMEOUT = 500  # seconds, adjust if necessary


def parse_arguments():
    parser = argparse.ArgumentParser(
        description='App for measuring BLE throughput over ACL.',
        epilog='How to run python script for hci0 -> rx and hci1 -> tx: \
                sudo python main.py -i 0 1 -m rx tx \
                -t path/to/custom_transport_directory -cf config.yaml')
    parser.add_argument('-i', '--indexes', type=str, nargs='*',
                        help='specify adapters indexes', default=[0, 1])
    parser.add_argument('-m', '--modes', type=str, nargs="*",
                        help='devices modes - receiver, transmitter',
                        choices=['rx', 'tx'], default=['rx', 'tx'])
    parser.add_argument('-t', '--transport_directory', type=str, nargs='*',
                        help='specify hci transport directory path. \
                        Use for transport other than the default linux socket.',
                        default=["default"])
    parser.add_argument('-cf', '--config_file', type=str, nargs="*",
                        help='configuration file for devices',
                        default=["config.yaml"])
    try:
        args = parser.parse_args()
    except Exception as e:
        print(traceback.format_exc())

    print(f"Indexes: {args.indexes}")
    print(f"Modes: {args.modes}")
    print(f"Transport directory: {args.transport_directory}")

    return args


def get_dev_addr_and_type(hci_indexes: list, transport_directory: str):
    if (len(hci_indexes) != 2):
        raise Exception("HCI index error.")
    manager = multiprocessing.Manager()
    addr_list = manager.list()
    check_addrs_proc = multiprocessing.Process(target=check_addr.check_addr,
                                               name="Check addresses",
                                               args=(hci_indexes, addr_list,
                                                     transport_directory))
    check_addrs_proc.start()
    print("check_addrs_proc pid: ", check_addrs_proc.pid)
    check_addrs_proc.join()
    dev_addr_type_list = []
    for i in range(0, len(addr_list)):
        dev_addr_type_list.append((hci_indexes[i],) + addr_list[i])
    return dev_addr_type_list


def change_config_var(filename: str, group: str, variable: str,
                      new_value: int):
    with open(filename, "r") as file:
        cfg = yaml.safe_load(file)

    if group:
        cfg[group][variable] = new_value
    else:
        cfg[variable] = new_value

    with open(filename, "w") as file:
        yaml.safe_dump(cfg, file, indent=1, sort_keys=False,
                       default_style=None, default_flow_style=False)

def generate_long_term_key():
    rand_val = random.getrandbits(128)
    return rand_val.to_bytes(16, byteorder='little')


def get_init_dict(filename: str, args_list: list, modes: list, dir: str,
                  transport_directory: str):
    ini = {
        modes[0]: {
            "dev_index": args_list[0][0],
            "own_address_type": args_list[0][1],
            "own_address": args_list[0][2],
            "peer_dev_index": args_list[1][0],
            "peer_address_type": args_list[1][1],
            "peer_address": args_list[1][2]
        },
        modes[1]: {
            "dev_index": args_list[1][0],
            "own_address_type": args_list[1][1],
            "own_address": args_list[1][2],
            "peer_dev_index": args_list[0][0],
            "peer_address_type": args_list[0][1],
            "peer_address": args_list[0][2]
        },
        "test_dir": dir,
        "transport_directory": transport_directory,
        "ltk": hex(random.getrandbits(128))
    }

    with open(filename, 'w') as file:
        yaml.safe_dump(ini, file, indent=1, sort_keys=False)

    return ini


def run_once(modes: list, cfg_file: str, init_file: str):
    list_proc = []
    for mode in modes:
        proc = subprocess.Popen(["python", "hci_device.py", "-m",
                                 mode, "-if", init_file, "-cf", cfg_file])
        print("start subprocess pid: ", proc.pid)
        list_proc.append(proc)
    try:
        for proc in list_proc:
            proc.wait(PROCESS_TIMEOUT)
    except subprocess.TimeoutExpired:
        for proc in list_proc:
            print("TimeoutExpired subprocess pid: ", proc.pid)
            proc.terminate()
        for proc in list_proc:
            proc.wait()
        return -1

    for proc in list_proc:
        print("stop subprocess pid: ", proc.pid)
        proc.terminate()
        proc.wait()
    return 0


def testing_variable_influence(cfg: dict, modes: list, cfg_file: str,
                               init_file: str, init_dict: dict,
                               save_to_file: bool):
    tp_test_counter = 1
    changed_params_list = []
    averages = []
    cfg_group = cfg["test"]["change_param_group"]
    cfg_variable = cfg["test"]["change_param_variable"]
    cfg_start_val = cfg["test"]["start_value"]
    cfg_stop_val = cfg["test"]["stop_value"]
    cfg_step = cfg["test"]["step"]
    data_type = cfg["tp"]["data_type"]
    total_iterations = math.ceil((cfg_stop_val - cfg_start_val) / cfg_step)
    average_tp_csv_path = init_dict["test_dir"] + "/average_rx_tp.csv"

    with open(average_tp_csv_path, "w") as file:
        file.write(f"Average throughput [{data_type}ps]\n")

    for i in range(cfg_start_val, cfg_stop_val, cfg_step):
        changed_params_list.append(i)

        if cfg_group and cfg_variable:
            print(f"Current param value: {i}")
            num_of_params_to_change = len(cfg_variable)

            for j in range(0, num_of_params_to_change):
                change_config_var(filename=cfg_file, group=cfg_group[j],
                                  variable=cfg_variable[j], new_value=i)

        print(f"Running test: {tp_test_counter}/{total_iterations}...")
        rc = run_once(modes, cfg_file, init_file)
        if rc != 0:
            print(f"Test {i} failed. Closing...")
            return

        tp_test_counter += 1

    with open(average_tp_csv_path, "r") as file:
        csv_reader = csv.reader(file)
        next(csv_reader)
        for row in csv_reader:
            averages.append(float(*row))

    fig, ax = plt.subplots()
    ax.plot(changed_params_list[:len(averages)], averages, '-k')
    ax.set_ylabel(f"Average throughput [{data_type}/s]")
    ax.set_xlabel("Changed parameter/next iteration")
    ax.set_title("Average througput")

    if save_to_file:
        name = init_dict["test_dir"] + "/average_tps"
        plt.savefig(fname=name, format='png')

    plt.show(block=True)


def main():
    args = parse_arguments()

    init_file = "init.yaml"
    cfg_file = args.config_file[0]
    if (isinstance(args.transport_directory, list)):
        args.transport_directory = args.transport_directory.pop()
    else:
        args.transport_directory = args.transport_directory

    with open(cfg_file, "r") as file:
        cfg = yaml.safe_load(file)

    addr_list = get_dev_addr_and_type(args.indexes, args.transport_directory)
    if len(addr_list) != len(args.indexes):
        raise Exception("No device address received. Check HCI indexes.")
    print(f"Received: {addr_list}")

    test_dir_path = util.create_test_directory()
    init_dict = get_init_dict(filename=init_file, args_list=addr_list,
                              modes=args.modes, dir=test_dir_path,
                              transport_directory=args.transport_directory)

    util.copy_config_files_to_test_directory([init_file, cfg_file],
                                             init_dict["test_dir"])

    try:
        if cfg["flag_testing"]:
            testing_variable_influence(cfg, args.modes, *args.config_file,
                                       init_file, init_dict, True)
        else:
            print(f"Running test...")
            rc = run_once(args.modes, cfg_file, init_file)
            if rc != 0:
                print("Test failed.")

        print("Finished. Closing...")

    except KeyboardInterrupt:
        pass
    except Exception as e:
        print(traceback.format_exc())
    finally:
        # Set default ownership for dirs and files
        util.set_default_chmod_recurs(os.getcwd() + "/tests")
        sys.exit()


if __name__ == "__main__":
    main()
