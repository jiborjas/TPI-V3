# TP Integrador - Variante 3: Caja de comandos fisica
# STM32F103C8T6 (Blue Pill) + libopencm3 + FreeRTOS
PROJECT = main
TARGET = $(PROJECT).elf
BUILD_DIR = bin
APP_CONFIG_HEADER = config/app_config.h

# Rutas base (ver third_party/README.md para obtener las dependencias)
THIRD_PARTY_DIR ?= third_party
COMMON_DIR ?= $(THIRD_PARTY_DIR)/common
LIBOPENCM3_DIR ?= $(THIRD_PARTY_DIR)/libopencm3
FREERTOS_DIR ?= $(THIRD_PARTY_DIR)/FreeRTOS-Kernel

HEAP_4_SRC := $(FREERTOS_DIR)/portable/MemMang/heap_4.c
ifeq ($(wildcard $(HEAP_4_SRC)),)
HEAP_4_SRC := $(FREERTOS_DIR)/heap_4.c
endif

MCU = cortex-m3
CDEFS = -DSTM32F1

CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

INCLUDES  = -I$(LIBOPENCM3_DIR)/include
INCLUDES += -I$(COMMON_DIR)
INCLUDES += -I$(FREERTOS_DIR)
INCLUDES += -I$(FREERTOS_DIR)/include
INCLUDES += -I$(FREERTOS_DIR)/portable/GCC/ARM_CM3
INCLUDES += -I./config
INCLUDES += -I./app
INCLUDES += -I./drivers
INCLUDES += -I./platform
INCLUDES += -I./protocol
INCLUDES += -I./

CFLAGS = -mcpu=$(MCU) -mthumb -Wall -Wextra -O0 -g -ffunction-sections -fdata-sections $(INCLUDES) $(CDEFS) -nostdlib
LDSCRIPT = $(COMMON_DIR)/linker.ld
LDFLAGS = -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(PROJECT).map -T$(LDSCRIPT)
LDLIBS = -L$(LIBOPENCM3_DIR)/lib -lopencm3_stm32f1 -lc -lgcc -lnosys

FREERTOS_SRC = \
    $(FREERTOS_DIR)/list.c \
    $(FREERTOS_DIR)/queue.c \
    $(FREERTOS_DIR)/tasks.c \
    $(FREERTOS_DIR)/portable/GCC/ARM_CM3/port.c \
    $(HEAP_4_SRC)

LOCAL_SRC = \
    main.c \
    platform/system_init.c \
    drivers/uart_comm.c \
    drivers/board_io.c \
    protocol/protocol.c \
    protocol/parser.c \
    app/command_box.c \
    app/signals.c \
    app/tasks.c

LOCAL_OBJ = $(patsubst %.c,$(BUILD_DIR)/%.o,$(LOCAL_SRC))
FREERTOS_OBJ = $(patsubst $(FREERTOS_DIR)/%.c,$(BUILD_DIR)/freertos/%.o,$(FREERTOS_SRC))
OBJ = $(LOCAL_OBJ) $(FREERTOS_OBJ)

.PHONY: all clean flash size help check-deps openocd gdb

all: check-deps $(LIBOPENCM3_DIR)/lib/libopencm3_stm32f1.a $(BUILD_DIR)/$(TARGET)

check-deps:
	@test -f "$(COMMON_DIR)/linker.ld" || (echo "Falta $(COMMON_DIR)/linker.ld" && exit 1)
	@test -f "$(LIBOPENCM3_DIR)/include/libopencm3/stm32/usart.h" || (echo "Falta libopencm3 en $(LIBOPENCM3_DIR) (ver third_party/README.md)" && exit 1)
	@test -f "$(FREERTOS_DIR)/include/FreeRTOS.h" || (echo "Falta FreeRTOS-Kernel en $(FREERTOS_DIR) (ver third_party/README.md)" && exit 1)

$(OBJ): $(APP_CONFIG_HEADER)

$(BUILD_DIR)/$(TARGET): $(OBJ) $(LIBOPENCM3_DIR)/lib/libopencm3_stm32f1.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
	$(OBJCOPY) -O ihex $@ $(BUILD_DIR)/$(PROJECT).hex
	$(OBJCOPY) -O binary $@ $(BUILD_DIR)/$(PROJECT).bin

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/freertos/%.o: $(FREERTOS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBOPENCM3_DIR)/lib/libopencm3_stm32f1.a:
	$(MAKE) -C $(LIBOPENCM3_DIR)

flash: $(BUILD_DIR)/$(TARGET)
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program $< verify reset exit"

openocd:
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg

gdb: $(BUILD_DIR)/$(TARGET)
	gdb-multiarch $< -ex "target remote localhost:3333"

size: $(BUILD_DIR)/$(TARGET)
	arm-none-eabi-size $<

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Comandos disponibles:"
	@echo "  make           -> Compila el firmware"
	@echo "  make flash     -> Flashea el binario via OpenOCD (ST-Link)"
	@echo "  make gdb       -> Conecta GDB al target"
	@echo "  make openocd   -> Lanza el servidor OpenOCD"
	@echo "  make size      -> Muestra el uso de memoria"
	@echo "  make clean     -> Limpia la carpeta bin/"
