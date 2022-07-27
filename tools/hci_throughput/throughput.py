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

import time
import matplotlib.pyplot as plt
import csv
import struct
import argparse
import traceback

data_types = ['kb', 'kB']


def parse_arguments():
    parser = argparse.ArgumentParser(
        description='Plot throughput from the csv file.',
        epilog='How to run script: \
                python throughput.py -f tests/Wed_Apr_13_08:36:29_2022/tp_receiver.csv -s 0.1')

    parser.add_argument('-f', '--file', type=str, nargs='*',
                        help='csv file path', default=["tp_receiver"])
    parser.add_argument('-s', '--samp_t', type=float, nargs='*',
                        help='specify throughput sample time', default=1.0)
    try:
        args = parser.parse_args()
    except Exception as e:
        print(traceback.format_exc())
    return args


def gen_data(num_of_bytes_in_packet: int,
             last_number_from_previous_data_packet: int):
    counter = last_number_from_previous_data_packet + 1
    rem = num_of_bytes_in_packet % 4
    valid_data_len = int((num_of_bytes_in_packet - rem) / 4)
    total_data_len = valid_data_len + rem
    data = [0] * total_data_len
    for i in range(rem, total_data_len):
        data[i] = counter
        counter += 1
    last_value = data[len(data) - 1]
    if rem:
        fmt = "<" + str(rem) + "B" + str(valid_data_len) + "I"
    else:
        fmt = "<" + str(valid_data_len) + "I"
    data_ba = struct.pack(fmt, *data)
    return data_ba, last_value


class Throughput():
    def __init__(
            self,
            name="tp_chart",
            mode="rx",
            total_packets_number=0,
            bytes_number_in_packet=0,
            throughput_data_type='kb',
            flag_plot_packets=True,
            sample_time=1,
            test_directory=None):
        self.name = name
        self.mode = mode
        self.total_packets_number = total_packets_number
        self.bytes_number_in_packet = bytes_number_in_packet
        self.predef_packet_key = int(
            (bytes_number_in_packet - (bytes_number_in_packet % 4)) / 4)
        self.total_bits_number = bytes_number_in_packet * 8
        assert throughput_data_type in data_types
        self.throughput_data_type = throughput_data_type
        self.flag_plot_packets = flag_plot_packets
        self.sample_time = sample_time
        self.test_directory = test_directory

        if self.test_directory is not None:
            self.csv_file_name = self.test_directory + "/" + \
                time.strftime("%Y_%m_%d_%H_%M_%S_") + self.name + ".csv"
        else:
            self.csv_file_name = time.strftime(
                "%Y_%m_%d_%H_%M_%S_") + self.name + ".csv"
        self.clean_csv_file()

    def calc_throughput(self, current_num, last_num, current_time, last_time):
        if self.throughput_data_type == 'kb':
            return float(
                (((current_num - last_num) * self.total_bits_number) /
                 (current_time - last_time)) / 1000)
        elif self.throughput_data_type == 'kB':
            return float(
                (((current_num - last_num) * self.bytes_number_in_packet) /
                 (current_time - last_time)) / 1000)

    def clean_csv_file(self):
        file = open(self.csv_file_name, 'w')
        file.write("Time,Packet\n")

    def append_to_csv_file(
            self,
            timestamp: float = 0.0,
            packet_number: int = 0):
        with open(self.csv_file_name, "a") as file:
            csv_writer = csv.writer(file)
            csv_writer.writerow([timestamp, packet_number])

    def get_average(self, packet_numbers, timestamps):
        if self.throughput_data_type == 'kb':
            average_tp = ((packet_numbers * self.total_bits_number)
                          / (timestamps[-1] - timestamps[0])) / 1000
        elif self.throughput_data_type == 'kB':
            average_tp = ((packet_numbers * self.bytes_number_in_packet)
                          / (timestamps[-1] - timestamps[0])) / 1000
        return average_tp

    def save_average(self, tp_csv_filename=None):
        if self.mode == "rx":
            timestamps = []
            packet_numbers = []

            if tp_csv_filename is None:
                tp_csv_filename = self.csv_file_name
            else:
                tp_csv_filename += ".csv"

            with open(tp_csv_filename, "r") as file:
                csv_reader = csv.reader(file)
                next(csv_reader)
                for row in csv_reader:
                    timestamps.append(float(row[0]))
                    packet_numbers.append(float(row[1]))

            average_tp = self.get_average(packet_numbers[-1], timestamps)
            print(
                f"Average rx throughput: {round(average_tp, 3)} {self.throughput_data_type}ps")

            with open(self.test_directory + "/average_rx_tp.csv", "a") as file:
                csv_writer = csv.writer(file)
                csv_writer.writerow([average_tp])

    def plot_tp_from_file(self, filename: str = None, sample_time: float = 1,
                          save_to_file: bool = True):
        timestamps = []
        packet_numbers = []

        if filename is None:
            filename = self.csv_file_name
        print("Results:", filename)

        with open(filename, "r") as file:
            csv_reader = csv.reader(file)
            next(csv_reader)
            for row in csv_reader:
                timestamps.append(float(row[0]))
                packet_numbers.append(float(row[1]))

        last_time = 0
        last_number = packet_numbers[0]
        throughput = []
        offset = timestamps[0]

        for i in range(0, len(timestamps)):
            timestamps[i] -= offset
            if timestamps[i] - last_time > sample_time:
                throughput.append((timestamps[i],
                                   self.calc_throughput(packet_numbers[i],
                                                        last_number,
                                                        timestamps[i],
                                                        last_time)))
                last_time = timestamps[i]
                last_number = packet_numbers[i]

        average_tp = self.get_average(packet_numbers[-1], timestamps)

        fig, ax = plt.subplots()
        if self.flag_plot_packets:
            ax2 = ax.twinx()

        ax.plot(*zip(*throughput), 'k-')
        if self.flag_plot_packets:
            ax2.plot(timestamps, packet_numbers, 'b-')

        ax.set_title(self.name)
        ax.set_ylabel(f"Throughput [{self.throughput_data_type}/s]")
        ax.set_xlabel("Time [s]")
        ax.text(0.9, 1.02, f"Average: {round(average_tp, 3)}"
                f"{self.throughput_data_type}ps", transform=ax.transAxes,
                color='k')
        if self.flag_plot_packets:
            ax2 = ax2.set_ylabel(f"Packets [Max:{len(packet_numbers)}]",
                                 color='b')

        if save_to_file:
            path = filename.replace(".csv", ".png")
            plt.savefig(path)

        plt.show(block=True)


if __name__ == "__main__":
    args = parse_arguments()
    tp = Throughput(bytes_number_in_packet=247)
    tp.plot_tp_from_file(*args.file, args.samp_t[0], save_to_file=False)
