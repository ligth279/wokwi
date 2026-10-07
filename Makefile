# STM32F103C8 fault-injection study - build with arm-none-eabi-gcc.
#
#   make BUILD=smoke     -> step 0 simulator/peripheral probe (ARMv7-M)
#   make BUILD=i2ctest   -> step 1 I2C sensor + stuck-low chip test
#   make BUILD=uartrx    -> software UART receiver probe
#   make BUILD=baseline  -> FreeRTOS application, no protection
#   make run BUILD=smoke -> build + run in Wokwi CLI (needs WOKWI_CLI_TOKEN)
#   make chips           -> compile custom Wokwi chips to WASM
#   make unit            -> host unit tests (FAULT parser, framework logic)
#   make size            -> flash/RAM usage of the selected build
#   (Zed/clangd: flags are in .clangd)

BUILD ?= smoke
WOKWI_CLI ?= $(shell command -v wokwi-cli 2>/dev/null || echo $(HOME)/.wokwi/bin/wokwi-cli)

PREFIX  := arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

BUILD_DIR := build/$(BUILD)
TARGET    := $(BUILD_DIR)/firmware

DRV      := Drivers
HAL_DIR  := $(DRV)/stm32f1xx-hal-driver
CMSIS    := $(DRV)/cmsis-core/CMSIS/Core/Include
DEV_DIR  := $(DRV)/cmsis-device-f1

HAL_MODULES := hal hal_cortex hal_rcc hal_rcc_ex hal_gpio hal_gpio_ex hal_dma \
               hal_i2c hal_crc hal_wwdg hal_tim hal_tim_ex hal_flash \
               hal_flash_ex hal_exti
HAL_SRCS := $(foreach m,$(HAL_MODULES),$(HAL_DIR)/Src/stm32f1xx_$(m).c)

COMMON_SRCS := \
	$(DEV_DIR)/Source/Templates/system_stm32f1xx.c \
	Core/Src/board.c \
	Core/Src/stm32f1xx_it.c \
	Core/Src/syscalls.c \
	Core/Src/debug_fault.c \
	Logging/Src/log.c \
	$(HAL_SRCS)

RTOS_DIR  := $(DRV)/FreeRTOS-Kernel
RTOS_SRCS := $(RTOS_DIR)/tasks.c $(RTOS_DIR)/queue.c $(RTOS_DIR)/list.c \
             RTOS/port_wokwi_cm3/port.c $(RTOS_DIR)/portable/MemMang/heap_4.c

ASM_SRCS := $(DEV_DIR)/Source/Templates/gcc/startup_stm32f103xb.s

ifeq ($(BUILD),smoke)
  APP_SRCS := Tests/smoke/smoke_main.c
  DEFS     :=
else ifeq ($(BUILD),i2ctest)
  APP_SRCS := Tests/i2c/i2c_main.c App/Src/sensor.c Recovery/Src/i2c_recovery.c
  DEFS     :=
else ifeq ($(BUILD),uartrx)
  APP_SRCS := Tests/uart/uart_rx_main.c Logging/Src/soft_uart_rx.c
  DEFS     :=
else ifeq ($(BUILD),baseline)
  APP_SRCS := Core/Src/main.c App/Src/app_tasks.c App/Src/control.c App/Src/console.c \
              App/Src/sensor.c Logging/Src/soft_uart_rx.c \
              FaultInjection/Src/fault_catalog.c FaultInjection/Src/fault_cmd.c \
              FaultInjection/Src/fault_inject.c FaultInjection/Src/fault_fw.c \
              FaultInjection/Src/fi_port_stm32.c FaultInjection/Src/fi_test.c $(RTOS_SRCS)
  DEFS     := -DUSE_FREERTOS -DPROTECTED=0
else
  $(error Unknown BUILD '$(BUILD)'; valid: smoke i2ctest uartrx baseline)
endif

SRCS := $(COMMON_SRCS) $(APP_SRCS)

INCS := -ICore/Inc -ILogging/Inc -IApp/Inc -IRecovery/Inc -IFaultInjection/Inc -I$(HAL_DIR)/Inc -I$(DEV_DIR)/Include -I$(CMSIS) \
        -I$(RTOS_DIR)/include -IRTOS/port_wokwi_cm3

# Instruction set. The target is a Cortex-M3, but Wokwi loses Thumb-2
# IT-block state on every exception (smoke test CORE_IT_STATE), which would
# silently corrupt interrupted code. Every experiment build is therefore
# compiled for the ARMv6-M subset (no IT instruction), which the M3 executes
# natively; `check-it` verifies the linked image contains no IT instruction.
# Only the smoke probe uses full ARMv7-M (it needs M3-only instructions to
# characterise the simulator).
ifeq ($(BUILD),smoke)
  MCU := -mcpu=cortex-m3 -mthumb
else
  MCU := -mcpu=cortex-m0 -mthumb -masm-syntax-unified
endif
CFLAGS := $(MCU) -std=gnu11 -Os -g3 -Wall -Wextra -Wno-unused-parameter \
          -ffunction-sections -fdata-sections -fno-common \
          -DSTM32F103xB -DUSE_HAL_DRIVER -DUSER_VECT_TAB_ADDRESS $(DEFS) $(EXTRA_DEFS) $(INCS) -MMD -MP
ASFLAGS := $(MCU) -x assembler-with-cpp
LDSCRIPT := Linker/STM32F103C8_FLASH.ld
LDFLAGS := $(MCU) -T$(LDSCRIPT) --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(TARGET).map -Wl,--print-memory-usage \
           -Wl,--no-warn-rwx-segments

OBJS := $(addprefix $(BUILD_DIR)/obj/,$(SRCS:.c=.o)) \
        $(addprefix $(BUILD_DIR)/obj/,$(ASM_SRCS:.s=.o))

.PHONY: all clean size run chips unit
all: $(TARGET).elf $(TARGET).hex

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(TARGET).hex: $(TARGET).elf
ifneq ($(BUILD),smoke)
	@n=$$($(PREFIX)objdump -d $< | grep -cP '\tit[te]{0,3}\s') ; \
	  if [ "$$n" != "0" ]; then echo "ERROR: $$n IT instructions in $< (see Makefile ISA note)"; exit 1; fi; \
	  echo "check-it: 0 IT instructions in $<"
endif
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/obj/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

size: $(TARGET).elf
	$(SIZE) -A -d $< | grep -E '^\.(isr_vector|text|rodata|ARM|init_array|fini_array|data|bss|noinit)|Total'
	$(SIZE) -B -d $<

run: all $(CHIPS)
	$(WOKWI_CLI) --elf $(TARGET).elf --timeout $(or $(TIMEOUT),10000) \
	  --serial-log-file $(BUILD_DIR)/serial.log $(WOKWI_ARGS) .

# Custom Wokwi chips (C -> WASM). First run auto-installs WASI-SDK to ~/.wokwi.
CHIPS := chips/temp-sensor.chip.wasm chips/i2c-stuck.chip.wasm
chips: $(CHIPS)
# serialise: the first chip compile may install WASI-SDK, which is not parallel-safe
chips/temp-sensor.chip.wasm: | chips/i2c-stuck.chip.wasm
chips/%.chip.wasm: chips/%.chip.c
	$(WOKWI_CLI) chip compile -o $@ $<

# Host unit tests (native gcc, no hardware).
unit:
	@mkdir -p build/unit
	gcc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -IFaultInjection/Inc \
	  Tests/unit/test_fault_cmd.c FaultInjection/Src/fault_cmd.c FaultInjection/Src/fault_catalog.c FaultInjection/Src/fi_test.c \
	  -o build/unit/test_fault_cmd
	./build/unit/test_fault_cmd
	gcc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -DFI_HOST_TEST \
	  -ITests/unit/host -IFaultInjection/Inc \
	  Tests/unit/test_fault_fw.c FaultInjection/Src/fault_fw.c FaultInjection/Src/fault_catalog.c \
	  FaultInjection/Src/fi_test.c FaultInjection/Src/fault_inject.c -o build/unit/test_fault_fw
	./build/unit/test_fault_fw

clean:
	rm -rf build

-include $(OBJS:.o=.d)
