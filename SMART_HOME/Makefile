##############################################################################
# SMART_HOME —— STM32F429 智能家居中控终端（实验三）
#   板卡: 正点原子阿波罗 V2 STM32F429 (STM32F429IGTx, LQFP176)
#         配套 DNF429-EB 嵌入式学习实验箱 + 7 寸 800x480 RGB 屏 + ATK-OV5640
#   时钟: HSE 25MHz -> PLL -> SYSCLK 180MHz / HCLK 180MHz / APB1 45MHz / APB2 90MHz
#         LTDC 像素时钟 = PLLSAI(240/4) / 2 = 30MHz
#         SDRAM 时钟 = HCLK/2 = 90MHz
#   用法: make all
##############################################################################
TARGET = SMART_HOME
BUILD  = build
PREFIX = arm-none-eabi-
CC     = $(PREFIX)gcc
AS     = $(PREFIX)gcc -x assembler-with-cpp
CP     = $(PREFIX)objcopy
SZ     = $(PREFIX)size

# ---------------------------------------------------------------------------
# 应用层源码
# ---------------------------------------------------------------------------
C_SOURCES  = Src/main.c
C_SOURCES += Src/gpio.c
C_SOURCES += Src/stm32f4xx_it.c
C_SOURCES += Src/stm32f4xx_hal_msp.c
C_SOURCES += Src/system_stm32f4xx.c
C_SOURCES += Src/syscalls.c
C_SOURCES += Src/sysmem.c
C_SOURCES += Src/bsp_delay.c
C_SOURCES += Src/bsp_led.c
C_SOURCES += Src/bsp_key.c
C_SOURCES += Src/bsp_sdram.c
C_SOURCES += Src/bsp_lcd.c
C_SOURCES += Src/bsp_font.c
C_SOURCES += Src/bsp_adc.c
C_SOURCES += Src/dht11.c
C_SOURCES += Src/ov5640.c
C_SOURCES += Src/bsp_dcmi.c
C_SOURCES += Src/app.c

# ---------------------------------------------------------------------------
# STM32F4xx HAL（只编译本工程真正用到的模块）
# ---------------------------------------------------------------------------
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
C_SOURCES += $(HAL)/stm32f4xx_hal_adc.c
C_SOURCES += $(HAL)/stm32f4xx_hal_adc_ex.c
C_SOURCES += $(HAL)/stm32f4xx_hal_ltdc.c
C_SOURCES += $(HAL)/stm32f4xx_hal_dcmi.c
C_SOURCES += $(HAL)/stm32f4xx_hal_sdram.c
C_SOURCES += $(HAL)/stm32f4xx_ll_fmc.c
C_SOURCES += $(HAL)/stm32f4xx_hal_tim.c
C_SOURCES += $(HAL)/stm32f4xx_hal_tim_ex.c

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
# --no-warn-rwx-segments: binutils >= 2.39 会对 CubeIDE 标准链接脚本的
# FLASH 段报一条 "LOAD segment with RWX permissions" 提示（对 STM32 无影响，
# 只是为了让编译输出保持 0 warning）。用更老的工具链时删掉这一段即可。
LDFLAGS  = $(MCU) -specs=nano.specs -specs=nosys.specs -T$(LDSCRIPT) -lc -lm \
           -Wl,-Map=$(BUILD)/$(TARGET).map,--cref -Wl,--gc-sections \
           -Wl,--no-warn-rwx-segments

OBJECTS  = $(addprefix $(BUILD)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

# ---------------------------------------------------------------------------
# 字库自动再生
#   源码里的中文字符串（config.h 的姓名、app.c 的界面文字）一变，
#   就重跑生成脚本，把用到的汉字全部打进 Inc/lcdfont.h。
#   前面加 "-" 是为了在没装 Python/Pillow 的机器上不阻塞构建（沿用旧字库）。
# ---------------------------------------------------------------------------
Inc/lcdfont.h: tools/gen_font.py tools/ui_strings.txt Inc/config.h Src/app.c
	-@py tools/gen_font.py Inc/lcdfont.h tools/ui_strings.txt .

$(BUILD)/bsp_font.o: Inc/lcdfont.h

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
	-mkdir $(BUILD)

clean:
	-rm -fR $(BUILD)
	-rmdir /s /q $(BUILD)

.PHONY: all clean
