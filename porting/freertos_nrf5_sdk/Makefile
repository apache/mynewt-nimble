PROJECT_NAME     := nimble_FreeRTOS_pca10040
TARGETS          := nrf52832_xxaa
OUTPUT_DIRECTORY := _build

NRF5_SDK_ROOT ?= ./nrf5_sdk
PROJ_ROOT := ../..

$(OUTPUT_DIRECTORY)/nrf52832_xxaa.out: \
  LINKER_SCRIPT  := nimble_FreeRTOS_gcc_nrf52.ld

# Source files common to all targets
SRC_FILES += \
  $(NRF5_SDK_ROOT)/components/libraries/log/src/nrf_log_backend_serial.c \
  $(NRF5_SDK_ROOT)/components/libraries/log/src/nrf_log_frontend.c \
  $(NRF5_SDK_ROOT)/components/libraries/button/app_button.c \
  $(NRF5_SDK_ROOT)/components/libraries/util/app_error.c \
  $(NRF5_SDK_ROOT)/components/libraries/util/app_error_weak.c \
  $(NRF5_SDK_ROOT)/components/libraries/timer/app_timer_freertos.c \
  $(NRF5_SDK_ROOT)/components/libraries/util/app_util_platform.c \
  $(NRF5_SDK_ROOT)/components/libraries/util/nrf_assert.c \
  $(NRF5_SDK_ROOT)/components/libraries/util/sdk_errors.c \
  $(NRF5_SDK_ROOT)/external/freertos/source/croutine.c \
  $(NRF5_SDK_ROOT)/external/freertos/source/event_groups.c \
  $(NRF5_SDK_ROOT)/external/freertos/source/portable/MemMang/heap_1.c \
  $(NRF5_SDK_ROOT)/external/freertos/source/list.c \
  $(NRF5_SDK_ROOT)/external/freertos/portable/GCC/nrf52/port.c \
  $(NRF5_SDK_ROOT)/external/freertos/portable/CMSIS/nrf52/port_cmsis.c \
  $(NRF5_SDK_ROOT)/external/freertos/portable/CMSIS/nrf52/port_cmsis_systick.c \
  $(NRF5_SDK_ROOT)/external/freertos/source/queue.c \
  $(NRF5_SDK_ROOT)/external/freertos/source/tasks.c \
  $(NRF5_SDK_ROOT)/external/freertos/source/timers.c \
  $(NRF5_SDK_ROOT)/components/boards/boards.c \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/clock/nrf_drv_clock.c \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/common/nrf_drv_common.c \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/gpiote/nrf_drv_gpiote.c \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/uart/nrf_drv_uart.c \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/nrf_soc_nosd/nrf_nvic.c \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/nrf_soc_nosd/nrf_soc.c \
  $(NRF5_SDK_ROOT)/components/libraries/bsp/bsp.c \
  $(NRF5_SDK_ROOT)/components/libraries/bsp/bsp_nfc.c \
  $(NRF5_SDK_ROOT)/external/segger_rtt/RTT_Syscalls_GCC.c \
  $(NRF5_SDK_ROOT)/external/segger_rtt/SEGGER_RTT.c \
  $(NRF5_SDK_ROOT)/external/segger_rtt/SEGGER_RTT_printf.c \
  $(NRF5_SDK_ROOT)/components/toolchain/gcc/gcc_startup_nrf52.S \
  $(NRF5_SDK_ROOT)/components/toolchain/system_nrf52.c \
  ./src/main.c \
  ./src/ble_task.c \
  ./src/os_port.c \
  ./src/cmsis_nvic.c \
  ./src/hal_timer.c \
  $(PROJ_ROOT)/porting/common/src/nimble_port.c \
  $(PROJ_ROOT)/porting/common/src/mem/mem.c \
  $(PROJ_ROOT)/porting/common/src/os/endian.c \
  $(PROJ_ROOT)/porting/common/src/os/os_cputime.c \
  $(PROJ_ROOT)/porting/common/src/os/os_cputime_pwr2.c \
  $(PROJ_ROOT)/porting/common/src/os/os_mbuf.c \
  $(PROJ_ROOT)/porting/common/src/os/os_mempool.c \
  $(PROJ_ROOT)/porting/common/src/os/os_msys_init.c \
  $(PROJ_ROOT)/porting/common/src/sysinit/sysinit.c \
  $(PROJ_ROOT)/nimble/src/ble_util.c \
  $(PROJ_ROOT)/nimble/src/hci_common.c \
  $(PROJ_ROOT)/nimble/host/src/ble_att_clt.c \
  $(PROJ_ROOT)/nimble/host/src/ble_att_cmd.c \
  $(PROJ_ROOT)/nimble/host/src/ble_att.c \
  $(PROJ_ROOT)/nimble/host/src/ble_att_svr.c \
  $(PROJ_ROOT)/nimble/host/src/ble_eddystone.c \
  $(PROJ_ROOT)/nimble/host/src/ble_gap.c \
  $(PROJ_ROOT)/nimble/host/src/ble_gattc.c \
  $(PROJ_ROOT)/nimble/host/src/ble_gatts_lcl.c \
  $(PROJ_ROOT)/nimble/host/src/ble_gatts.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_adv.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_atomic.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_cfg.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_conn.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_dbg.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_hci_cmd.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_hci_evt.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_hci.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_hci_util.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_id.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_log.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_mbuf.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_misc.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_pvcy.c \
  $(PROJ_ROOT)/nimble/host/src/ble_hs_startup.c \
  $(PROJ_ROOT)/nimble/host/src/ble_ibeacon.c \
  $(PROJ_ROOT)/nimble/host/src/ble_l2cap_coc.c \
  $(PROJ_ROOT)/nimble/host/src/ble_l2cap.c \
  $(PROJ_ROOT)/nimble/host/src/ble_l2cap_sig_cmd.c \
  $(PROJ_ROOT)/nimble/host/src/ble_l2cap_sig.c \
  $(PROJ_ROOT)/nimble/host/src/ble_monitor.c \
  $(PROJ_ROOT)/nimble/host/src/ble_sm_alg.c \
  $(PROJ_ROOT)/nimble/host/src/ble_sm_cmd.c \
  $(PROJ_ROOT)/nimble/host/src/ble_sm_lgcy.c \
  $(PROJ_ROOT)/nimble/host/src/ble_sm.c \
  $(PROJ_ROOT)/nimble/host/src/ble_sm_sc.c \
  $(PROJ_ROOT)/nimble/host/src/ble_store.c \
  $(PROJ_ROOT)/nimble/host/src/ble_store_util.c \
  $(PROJ_ROOT)/nimble/host/src/ble_uuid.c \
  $(PROJ_ROOT)/nimble/host/src/ble_uuid.c \
  $(PROJ_ROOT)/nimble/host/services/ans/src/ble_svc_ans.c \
  $(PROJ_ROOT)/nimble/host/services/gap/src/ble_svc_gap.c \
  $(PROJ_ROOT)/nimble/host/services/gatt/src/ble_svc_gatt.c \
  $(PROJ_ROOT)/nimble/host/services/ias/src/ble_svc_ias.c \
  $(PROJ_ROOT)/nimble/host/services/lls/src/ble_svc_lls.c \
  $(PROJ_ROOT)/nimble/host/services/tps/src/ble_svc_tps.c \
  $(PROJ_ROOT)/nimble/host/store/ram/src/ble_store_ram.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_sched.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_xcvr.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_whitelist.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_ctrl.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_hci.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_supp_cmd.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_adv.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_conn.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_resolv.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_conn_hci.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_rand.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_scan.c \
  $(PROJ_ROOT)/nimble/controller/src/ble_ll_hci_ev.c \
  $(PROJ_ROOT)/nimble/transport/ram/src/ble_hci_ram.c \
  $(PROJ_ROOT)/nimble/drivers/nrf52/src/ble_hw.c \
  $(PROJ_ROOT)/nimble/drivers/nrf52/src/ble_phy.c \
  $(PROJ_ROOT)/tinycrypt/src/aes_decrypt.c \
  $(PROJ_ROOT)/tinycrypt/src/aes_encrypt.c \
  $(PROJ_ROOT)/tinycrypt/src/cmac_mode.c \
  $(PROJ_ROOT)/tinycrypt/src/ecc.c \
  $(PROJ_ROOT)/tinycrypt/src/ecc_dh.c \
  $(PROJ_ROOT)/tinycrypt/src/utils.c \

# Include folders common to all targets
INC_FOLDERS += \
  ./config \
  $(NRF5_SDK_ROOT)/components \
  $(NRF5_SDK_ROOT)/components/toolchain/cmsis/include \
  $(NRF5_SDK_ROOT)/external/freertos/source/include \
  $(NRF5_SDK_ROOT)/external/freertos/config \
  $(NRF5_SDK_ROOT)/components/libraries/util \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/uart \
  $(NRF5_SDK_ROOT)/components/libraries/bsp \
  $(NRF5_SDK_ROOT)/components/device \
  $(NRF5_SDK_ROOT)/components/libraries/log \
  $(NRF5_SDK_ROOT)/components/libraries/button \
  $(NRF5_SDK_ROOT)/components/libraries/timer \
  $(NRF5_SDK_ROOT)/external/freertos/portable/CMSIS/nrf52 \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/gpiote \
  $(NRF5_SDK_ROOT)/external/segger_rtt \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/nrf_soc_nosd \
  $(NRF5_SDK_ROOT)/components/boards \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/hal \
  $(NRF5_SDK_ROOT)/components/toolchain/gcc \
  $(NRF5_SDK_ROOT)/components/toolchain \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/common \
  $(NRF5_SDK_ROOT)/components/drivers_nrf/clock \
  $(NRF5_SDK_ROOT)/external/freertos/portable/GCC/nrf52 \
  $(NRF5_SDK_ROOT)/components/libraries/log/src \
  ./include \
  $(PROJ_ROOT)/porting/common/include \
  $(PROJ_ROOT)/nimble/include \
  $(PROJ_ROOT)/nimble/host/include \
  $(PROJ_ROOT)/nimble/host/mesh/include \
  $(PROJ_ROOT)/nimble/host/services/ans/include \
  $(PROJ_ROOT)/nimble/host/services/gap/include \
  $(PROJ_ROOT)/nimble/host/services/gatt/include \
  $(PROJ_ROOT)/nimble/host/services/ias/include \
  $(PROJ_ROOT)/nimble/host/services/lls/include \
  $(PROJ_ROOT)/nimble/host/services/tps/include \
  $(PROJ_ROOT)/nimble/host/store/ram/include \
  $(PROJ_ROOT)/nimble/controller/include \
  $(PROJ_ROOT)/nimble/transport/ram/include \
  $(PROJ_ROOT)/nimble/drivers/nrf52/include \
  $(PROJ_ROOT)/tinycrypt/include \

# Libraries common to all targets
LIB_FILES += \

# C flags common to all targets
CFLAGS += -DNRF52_PAN_12
CFLAGS += -DNRF52_PAN_15
CFLAGS += -DNRF52_PAN_58
CFLAGS += -DFREERTOS
CFLAGS += -DNRF52_PAN_20
CFLAGS += -DNRF52_PAN_54
CFLAGS += -DNRF52
CFLAGS += -DNRF52_PAN_51
CFLAGS += -DNRF52_PAN_36
CFLAGS += -DCONFIG_GPIO_AS_PINRESET
CFLAGS += -DNRF52_PAN_64
CFLAGS += -DNRF52_PAN_55
CFLAGS += -DBOARD_PCA10040
CFLAGS += -DNRF52_PAN_31
CFLAGS += -DNRF52832
CFLAGS += -mcpu=cortex-m4
CFLAGS += -mthumb -mabi=aapcs
CFLAGS +=  -Wall -Werror -Og -g3
CFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16
# keep every function in separate section, this allows linker to discard unused ones
CFLAGS += -ffunction-sections -fdata-sections -fno-strict-aliasing
CFLAGS += -fno-builtin --short-enums 

# C++ flags common to all targets
CXXFLAGS += \

# Assembler flags common to all targets
ASMFLAGS += -x assembler-with-cpp
ASMFLAGS += -DNRF52_PAN_12
ASMFLAGS += -DNRF52_PAN_15
ASMFLAGS += -DNRF52_PAN_58
ASMFLAGS += -DFREERTOS
ASMFLAGS += -DNRF52_PAN_20
ASMFLAGS += -DNRF52_PAN_54
ASMFLAGS += -DNRF52
ASMFLAGS += -DNRF52_PAN_51
ASMFLAGS += -DNRF52_PAN_36
ASMFLAGS += -DCONFIG_GPIO_AS_PINRESET
ASMFLAGS += -DNRF52_PAN_64
ASMFLAGS += -DNRF52_PAN_55
ASMFLAGS += -DBOARD_PCA10040
ASMFLAGS += -DNRF52_PAN_31
ASMFLAGS += -DNRF52832

# Linker flags
LDFLAGS += -mthumb -mabi=aapcs -L $(TEMPLATE_PATH) -T$(LINKER_SCRIPT)
LDFLAGS += -mcpu=cortex-m4
LDFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16
# let linker to dump unused sections
LDFLAGS += -Wl,--gc-sections
# use newlib in nano version
LDFLAGS += --specs=nano.specs -lc -lnosys


.PHONY: $(TARGETS) default all clean help flash 

# Default target - first one defined
default: nrf52832_xxaa

# Print all targets that can be built
help:
	@echo following targets are available:
	@echo 	nrf52832_xxaa

TEMPLATE_PATH := $(NRF5_SDK_ROOT)/components/toolchain/gcc

include $(TEMPLATE_PATH)/Makefile.common

$(foreach target, $(TARGETS), $(call define_target, $(target)))

# Flash the program
flash: $(OUTPUT_DIRECTORY)/nrf52832_xxaa.hex
	@echo Flashing: $<
	nrfjprog --program $< -f nrf52 --sectorerase
	nrfjprog --reset -f nrf52

erase:
	nrfjprog --eraseall -f nrf52
