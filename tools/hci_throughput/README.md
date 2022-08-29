# HCI Throughput

Tool for measuring BLE throughput.

## Packages versions
Python 3.8.10 \
Matplotlib 3.5.1

Install all required packages with: 
```
sudo pip install -r requirements.txt
```

## Usage
### Prepare devices
This tool may be used with the existing controller or with any board with ```blehci``` app.

  - If you want to use the builtin PC controller, provide HCI index of the controller. Turn the Bluetooth ON on your device, run ```hciconfig``` in the terminal and get the HCI index. In the case below HCI index is equal to 0:

```
user@user:~$ hciconfig
hci0:	Type: Primary  Bus: USB
	BD Address: 64:BC:58:E2:9C:52  ACL MTU: 1021:4  SCO MTU: 96:6
	UP RUNNING 
	RX bytes:20003 acl:0 sco:0 events:3176 errors:0
	TX bytes:771246 acl:0 sco:0 commands:3174 errors:0
```

  - If you want to use the nimble controller, create the image and load the provided target (can be found under ```/targets``` for NRF52840 and NRF52832). 
    - NRF52840 may use USB or UART as HCI transport. The target is configured for USB by default.
    - NRF52832 uses UART as HCI transport. This requires some additional configuration. Get the tty path and run in the terminal:
    ```
    sudo btattach -B /dev/ttyACM0 -S 1000000
    ```
    Then proceed with ```hciconfig``` as shown above.

### Run tests


This tool opens a raw socket which requires running all scripts as ```sudo```. Copy the ```config.yaml.sample``` file, change the name to ```config.yaml``` and fill the parameters. 
Optionally pass the path to the custom transport directory if used. Run ```main.py``` as shown below:
```
sudo python main.py -i <hci_idx_1> <hci_idx_2> -m rx tx -t <path/to/custom_transport_directory> -cf config.yaml
```
Switch ```<hci_idx_1>``` and ```<hci_idx_2>``` to corresponding hci indexes present in your computer. ```-m```, ```-t``` and ```-cf``` may be omitted if the defaults are correct. \
The output provides the plots of measured throughput in ```kb``` or ```kB``` as predefined in ```config.yaml```. In addition to the throughput plots, when the ```flag_plot_packets``` is turned on, the number of packets transmitted/received in time is visualized.

**_When encountering issues with running tests, try to investigate the files in the log folder._**

#### Set ```config.yaml``` file
To run **once** the throughput measurement with given parameters, set the ```flag_testing``` to false.
```
flag_testing: false
```

To run the throughput measurements **more than once** with the same parameters and to generate the plot of average throughputs, set ```config.yaml``` as shown below:
```
show_tp_plots: false
flag_testing: true
test:
  change_param_group: null
  change_param_variable: null
  start_value: 0
  stop_value: 5
  step: 1
```
This configuration provides 5 measurements. The ```show_tp_plots``` flag is optionally set as ```false``` for speed, changing it to ```true``` will trigger rx and tx throughput plots at the end of every iteration.

To run the throughput measurement with some parameters changing within tests, fill config as below:
```
flag_testing: true
test:
  change_param_group:
  - conn
  - conn
  change_param_variable:
  - connection_interval_min
  - connection_interval_max
  start_value: 0x000A
  stop_value: 0x0320
  step: 20
```
This will run each test incrementing ```connection_interval_min``` and ```connection_interval_max``` by 20. the final plot will show the influence of the parameters change on the average throughput.

## Tools
The ```main.py``` script usees all tools mentioned below and it is advised to use it above all. Nevertheless, the sub-tools may be used separately as shown below.

### HCI device sub-tool
```hci_device.py``` is a tool that manages one hci device. User can provide parameters and run it as receiver or transmitter as shown below:
```
sudo python hci_device.py -m rx -oa 00:00:00:00:00:00 -oat 0 -di 0 -pa 00:00:00:00:00:00 -pat 0 -pdi 0 -cf config.yaml
```
Run ```python hci_device.py --help``` for parameters description. \
If properly configured ```init.yaml``` is present (it is created automatically while running ```main.py```), the script can be run like this:

```
sudo python hci_device.py -m tx -if init.yaml
```

### Check addr sub-tool
When given hci indexes, ```check_addr.py``` returns devices' address types and addresses. Optionally pass the path to the custom transport directory if used.
```
sudo python check_addr.py -i <hci_idx_1> <hci_idx_2> ... <hci_idx_N> -t <path/to/custom_transport_directory>
```

### Throughput sub-tool
The timestamps of the received packets are stored in csv files (```tp_receiver.csv``` and ```tp_transmitter.csv``` by default). If the program stopped in the middle of the measurements, you can still plot the values and get the average througput. Provide the filename, sample time and run the tool as shown below:
```
python throughput.py -f tp_receiver -s 0.1
```
