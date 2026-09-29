##############################################################################
# LED_BEEP 鐠烘垿鈹堥悘?+ 閾氬倿绂忛崳銊ョ杽妤?#   閺夊灝鐡? 濮濓絿鍋ｉ崢鐔风摍闂冩寧灏濈純?V2 STM32F429 (STM32F429IGTx, LQFP176)
#   閺冨爼鎸? HSE 25MHz -> PLL -> 180MHz
#   閻劍纭? make all
##############################################################################
TARGET = LED_BEEP
BUILD  = build
PREFIX = arm-none-eabi-
CC     = $(PREFIX)gcc
AS     = $(PREFIX)gcc -x assembler-with-cpp
CP     = $(PREFIX)objcopy
SZ     = $(PREFIX)size

C_SOURCES  = Src/main.c
C_SOURCES += Src/gpio.c
C_SOURCES += Src/stm32f4xx_it.c
C_SOURCES += Src/stm32f4xx_hal_msp.c
C_SOURCES += Src/system_stm32f4xx.c
C_SOURCES += Src/syscalls.c
C_SOURCES += Src/sysmem.c
C_SOURCES += Src/pcf8574.c
C_SOURCES += Src/softpwm.c

HAL = Drivers/STM32F4xx_HAL_Driver/Src
C_SOURCES += $(HAL)/stm32f4xx_hal.c
C_SOURCES += $(HAL)/stm32f4xx_hal_cortex.c
C_SOURCES += $(HAL)/stm32f4xx_hal_dma.c
C_SOURCES += $(HAL)/stm32f4xx_hal_dma_ex.c
C_SOURCES += $(HAL)/stm32f4xx_hal_exti.c
C_SOURCES += $(HAL)/stm32f4xx_hal_flash.c
C_SOURCES += $(HAL)/stm32f4xx_hal_flash_ex.c
C_SOURCES += $(HAL)/stm32f4xx_hal_flash_ramfunc.c
C_SOURCES += $(HAL)/stm32f4xx_hal_gpio.c
C_SOURCES += $(HAL)/stm32f4xx_hal_pwr.c
C_SOURCES += $(HAL)/stm32f4xx_hal_pwr_ex.c
C_SOURCES += $(HAL)/stm32f4xx_hal_rcc.c
C_SOURCES += $(HAL)/stm32f4xx_hal_rcc_ex.c

ASM_SOURCES = Startup/startup_stm32f429zitx.s

C_INCLUDES  = -IInc
C_INCLUDES += -IDrivers/STM32F4xx_HAL_Driver/Inc
C_INCLUDES += -IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy
C_INCLUDES += -IDrivers/CMSIS/Device/ST/STM32F4xx/Include
C_INCLUDES += -IDrivers/CMSIS/Include

C_DEFS = -DUSE_HAL_DRIVER -DSTM32F429xx
MCU    = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
OPT    = -Og
DEBUG  = -g3 -gdwarf-2

CFLAGS   = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) $(DEBUG) -Wall \
           -ffunction-sections -fdata-sections
ASFLAGS  = $(MCU) $(OPT) $(DEBUG) -Wall -ffunction-sections -fdata-sections
LDSCRIPT = STM32F429ZITX_FLASH.ld
LDFLAGS  = $(MCU) -specs=nano.specs -specs=nosys.specs -T$(LDSCRIPT) -lc -lm \
           -Wl,-Map=$(BUILD)/$(TARGET).map,--cref -Wl,--gc-sections

OBJECTS  = $(addprefix $(BUILD)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

$(BUILD)/%.o: %.c Makefile | $(BUILD)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD)/%.o: %.s Makefile | $(BUILD)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(CP) -O ihex $< $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(CP) -O binary -S $< $@

$(BUILD):
	if not exist "$@" mkdir "$@"

clean:
	-rm -fR $(BUILD)

.PHONY: all clean
