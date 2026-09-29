##############################################################################
# TIMER_LAB —— STM32F429 定时器综合实验
#   板卡: 正点原子阿波罗 V2 STM32F429 (STM32F429IGTx, LQFP176)
#   时钟: HSE 25MHz -> PLL -> 180MHz
#   用法: make all
##############################################################################
TARGET = TIMER_LAB
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
C_SOURCES += Src/oled.c
C_SOURCES += Src/display.c
C_SOURCES += Src/bsp_tim.c
C_SOURCES += Src/app.c

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
C_SOURCES += $(HAL)/stm32f4xx_hal_tim.c
C_SOURCES += $(HAL)/stm32f4xx_hal_tim_ex.c

ASM_SOURCES = Startup/startup_stm32f429zitx.s

C_INCLUDES  = -IInc
C_INCLUDES += -IDrivers/STM32F4xx_HAL_Driver/Inc
C_INCLUDES += -IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy
C_INCLUDES += -IDrivers/CMSIS/Device/ST/STM32F4xx/Include
C_INCLUDES += -IDrivers/CMSIS/Include

C_DEFS = -DUSE_HAL_DRIVER -DSTM32F429xx

# ---------------------------------------------------------------------------
# OLED 接口模式（模块背面的 BS0/BS1/BS2 决定，改这里不用动源码）
#   用法：
#       make clean all IFMODE=8080     默认，正点原子官方 8080 并口
#       make clean all IFMODE=SPI      4 线 SPI
#       make clean all IFMODE=IIC      IIC
#   注意换模式必须 make clean，否则旧的 .o 不会重编
# ---------------------------------------------------------------------------
IFMODE ?= 8080
ifeq ($(IFMODE),SPI)
C_DEFS += -DOLED_IF_MODE=OLED_IF_SPI
endif
ifeq ($(IFMODE),IIC)
C_DEFS += -DOLED_IF_MODE=OLED_IF_IIC
endif

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

# ---------------------------------------------------------------------------
# 字库自动再生
#   源码里的中文字符串（config.h 的姓名、app.c 的界面文字）一变，
#   就重跑生成脚本，把用到的汉字全部打进 Inc/oledfont.h。
#   前面加 "-" 是为了在没装 Python/Pillow 的机器上不阻塞构建（沿用旧字库）。
# ---------------------------------------------------------------------------
Inc/oledfont.h: tools/gen_font.py tools/ui_strings.txt Inc/config.h Src/app.c
	-@py tools/gen_font.py Inc/oledfont.h tools/ui_strings.txt .

$(BUILD)/oled.o: Inc/oledfont.h

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
