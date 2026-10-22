================================================================
 SMART_HOME —— STM32F429 智能家居中控终端（实验三）
================================================================
开发板：正点原子阿波罗 V2 STM32F429（STM32F429IGTx, LQFP176）
配套：  DNF429-EB 嵌入式学习实验箱
外设：  7 寸 800x480 RGB 屏（板上 RGBLCD 接口，LTDC 驱动）
        ATK-OV5640 摄像头（板上 P6 "OLED/CAMERA" 接口，DCMI + DMA）
        DHT11 温湿度（PB12 单总线）         —— 开发指南第 45 章
        电子电压表（板上 RV1 -> P10 跳线 -> PA5/ADC1_IN5）—— 第 32 章
        板载双色 LED（PB0 绿 DS1 / PB1 红 DS0）+ 4 个板载按键

时钟：  HSE 25MHz（PH0/PH1）-> PLLM=25 / PLLN=360 / PLLP=2 -> SYSCLK 180MHz
        HCLK 180MHz / APB1 45MHz / APB2 90MHz
        LTDC 像素时钟 = PLLSAI(1MHz x 240 / 4) / 2 = 30MHz
        SDRAM SDCLK   = HCLK / 2 = 90MHz
        Over-Drive 模式 + 稳压器 Scale1 + Flash 5 等待周期
调试：  SWD（PA13/PA14），用实验箱自带的 DAP 仿真器（CMSIS-DAP）

参考：《STM32F429 阿波罗开发指南》第 13/14/19/25/26/31/32/45/50 章
      《阿波罗V2 硬件参考手册》+《阿波罗V2 STM32F429开发板IO引脚分配表》
      OmniVision《OV5640 DVP Application Note》/ Linux mainline ov5640.c
      （这两份是 ov5640.c 寄存器表的取值来源，代码里逐块标了 [A]/[B]/[C]）

----------------------------------------------------------------
 一、功能总览
----------------------------------------------------------------
一块 800x480 的屏，两个页面，一个贯穿两个页面的预警状态机。

1) 中控首页（PAGE_HOME）
   - 顶栏：实验名称（EXP_TITLE）+ 右侧报警横幅
   - 学生信息行：姓名 / 学号 / 班级（全部来自 Inc/config.h）
   - 左侧三张数据卡片：温度 DHT11、湿度 DHT11、电压 RV1/ADC1_IN5
   - 右上"预警阈值"面板：三行 = 名称 + 上限值 + "正常/越限"标记
   - 右下"模块状态"面板：DHT11 状态字、电压表【原始 ADC 值 -> 换算电压】、
     摄像头就绪状态（实验要求"原始值与换算值同时显示"，就在这一行）
   - 底栏操作提示：K0/UP 切换页面  K1 重试摄像头  K2 立即测温湿度

2) 摄像头预览页（PAGE_CAMERA）
   - 左侧 640x480 预览区：OV5640 出的是 QVGA 320x240，
     用 BSP_LCD_BlitRGB565x2() 做 2 倍最近邻放大铺满，正好 640x480
   - 右侧状态栏：温度 / 湿度 / 电压 / 预警状态 / 预览正常或异常
   - 预警在摄像头页【持续生效】（这是实验要求，不是只在首页判断）
   - 初始化失败或中途失联时，预览区中央显示"摄像头初始化失败 / 无信号"，
     并提示按 K1 重新初始化，不会停在空白屏

3) 阈值预警状态机（三态，故障优先；回差防抖）
   ┌────────┬──────────────┬──────────────┬────────────────────────────┐
   │ 状态   │ 绿色 DS1 PB0 │ 红色 DS0 PB1 │ 触发条件                   │
   ├────────┼──────────────┼──────────────┼────────────────────────────┤
   │ NORMAL │ 常亮         │ 灭           │ 三项数据都有效且都没越限   │
   │ WARN   │ 灭           │ 2Hz 闪烁     │ 任意一项 > 对应上限        │
   │ FAULT  │ 灭           │ 常亮         │ 数据无效 / 传感器失联/超时 │
   └────────┴──────────────┴──────────────┴────────────────────────────┘
   判定优先级：只要 DHT11 或 ADC 有一个没拿到有效数据，直接进 FAULT，
   不再去看越限标志（故障优先，屏幕上也不会把旧数据标成"最新读数"）。
   从 FAULT 恢复时会把三个越限标志清零，按当前值重新判一次，
   避免"带病恢复"——刚修好就顶着一条旧的越限状态显示报警。

   回差（hysteresis，阈值都写在 Inc/config.h）：
   ┌──────┬────────────┬─────────┬───────────────┬──────────────────┐
   │ 项目 │ 上限(默认) │ 回差    │ 进入报警      │ 退出报警         │
   ├──────┼────────────┼─────────┼───────────────┼──────────────────┤
   │ 温度 │ 30.0 ℃     │ 1.0 ℃   │ value > 30.0  │ value <= 29.0    │
   │ 湿度 │ 70.0 %RH   │ 2.0 %RH │ value > 70.0  │ value <= 68.0    │
   │ 电压 │ 2.80 V     │ 0.05 V  │ value > 2.80  │ value <= 2.75    │
   └──────┴────────────┴─────────┴───────────────┴──────────────────┘
   进入用"严格大于上限"，退出用"小于等于上限 - 回差"，
   所以在阈值附近抖动时不会反复切换报警状态。

4) 局部刷新（"不闪"是怎么做到的）
   - LTDC 是硬件持续扫描 SDRAM 里的帧缓冲，CPU 改哪个像素屏上就变哪个像素，
     所以根本不需要"清屏 + 整屏重画"。
   - 静态部分（标题、卡片框、标签、阈值文字）只在进入页面时画一次。
   - 动态部分先把上次画的值记在 s_shown_xxx 里，只有变了才重画，
     重画时先 FillRect 清掉那一小块再写新字（字模背景不透明，所以不留残影）。
   - 电压值用 quantize2() 量化到 0.01V 再比较，ADC 那点末位抖动不会引起无意义刷新。

----------------------------------------------------------------
 二、按键操作约定（全部板载，不需要接线）
----------------------------------------------------------------
    K0  (PH3)  切换页面：中控首页 <-> 摄像头预览页
    UP  (PA0)  和 K0 同功能（WK_UP，高电平有效）
    K1  (PH2)  摄像头页：重新初始化 OV5640（先停采集，再重走一遍 OV5640_Init）
    K2  (PC13) 立即测温湿度一次（不用等 2.0s 的周期，现场验证很方便）

    消抖：每 10ms 扫一次，原始电平连续稳定 20ms 才认，且只在"按下边沿"
    返回一次事件 —— 长按不会重复触发。

----------------------------------------------------------------
 三、时钟方案与各模块参数（怎么算出来的）
----------------------------------------------------------------
1. 系统时钟树
     HSE               25 MHz     PH0/PH1 无源晶振
     PLL 输入           1 MHz     HSE / PLLM(25)
     PLL VCO          360 MHz     1MHz x PLLN(360)
     SYSCLK           180 MHz     VCO / PLLP(2)
     HCLK             180 MHz     SYSCLK / AHB_DIV1
     APB1              45 MHz     HCLK / 4  -> APB1 定时器时钟 90MHz
     APB2              90 MHz     HCLK / 1  -> APB2 定时器时钟 180MHz
     SDRAM SDCLK       90 MHz     HCLK / 2
     LTDC 像素时钟     30 MHz     PLLSAI: 1MHz x PLLSAIN(240) = 240MHz VCO
                                  -> /PLLSAIR(4) = 60MHz
                                  -> /PLLSAIDivR(2) = 30MHz
   * 180MHz 必须开 Over-Drive 并把稳压器设成 Scale1，Flash 用 5 个等待周期；
     本工程没有用任何 TIM，APB 定时器时钟列在这里只是为了说明"时钟树怎么读"。

2. LTDC 时序与帧率（宏都在 Inc/pinmap.h，换屏只改那几个宏）
     行总长 = 800(有效) + 48(HSW) + 88(HBP) + 40(HFP) = 976
     场总长 = 480(有效) +  3(VSW) + 32(VBP) + 13(VFP) = 528
     帧率   = 30MHz / (976 x 528) = 58.2 Hz
     帧缓冲 = 800 x 480 x 2B = 750 KiB，RGB565，放在 SDRAM 起始 0xC0000000
   * 面板只引出了高 5/6/5 位（低 3 位没接），所以 RGB565 是最自然的选择，
     也比 RGB888 省一半 SDRAM 带宽（750KiB/帧 而不是 1.5MiB/帧）。

3. SDRAM 刷新计数器 683 与 FMC 时序
     器件：W9825G6KH-6，32MB = 4 Bank x 8192 行 x 512 列 x 16 位
     刷新：每行 64ms / 8192 行 = 7.8125us 要刷一次
           90MHz 下一个计数 = 11.11ns
           COUNT = 7.8125us / 11.11ns - 20 = 703 - 20 = 683
           （减 20 是手册要求的余量）
     时序参数（单位是 SDRAM 时钟周期，按 90MHz 算）：
           参数                    写入值   依据
           ----------------------  -------  ----------------------------
           LoadToActiveDelay       2        tMRD = 2 clk
           ExitSelfRefreshDelay    7        tXSR = 70ns -> 7 clk
           SelfRefreshTime         4        tRAS = 42ns -> 4 clk
           RowCycleDelay           7        tRC  = 60ns -> 6 clk，取 7 留裕量
           WriteRecoveryTime       3        tWR  = 2 clk + 裕量
           RPDelay                 2        tRP  = 18ns -> 2 clk
           RCDDelay                2        tRCD = 18ns -> 2 clk
           CASLatency              3        -6 是 166MHz 器件，90MHz 下取 3 更稳
     上电序列：CLK_ENABLE -> 等 >=100us -> PALL -> AUTOREFRESH x8 ->
               LOAD_MODE(CAS=3, 单次写) -> ProgramRefreshRate(683)
     自检：BSP_SDRAM_Test() 在 0xC0200000 起测 64K 字（128KiB），
           先写递增数据读回比对，再用 0000/FFFF/5555/AAAA 四种图案查位间短路。

4. DCMI + DMA 的 Length = 38400 字（不是字节！）
     QVGA RGB565 一帧 = 320 x 240 x 2B = 153600 B
     DCMI_DR 是 32 位寄存器，DMA 外设/存储器都按 Word（4B）对齐搬运
     Length = 153600 / 4 = 38400 字
     外设：DMA2_Stream1_Channel1，P2M、MINC 开、Word/Word、NORMAL、HIGH
     中断：DCMI 和 DMA2_Stream1 都要开（优先级 5，低于 SysTick 的 0），
           回调里只置 s_frame_ready 标志，贴图在主循环做，不在中断里画屏。
     取下一帧前必须先 HAL_DCMI_Stop() 并清 OVRRI 溢出标志，
     否则新一帧可能从中间开始搬，图像会错位/撕裂。

5. DHT11 单总线（PB12，板上 4.7K 上拉）
     主机：拉低 >=18ms（本工程写 20ms）作为起始信号，然后释放总线
     从机：拉低 80us 应答，再拉高 80us
     数据：40 位 = 湿度整数/湿度小数/温度整数/温度小数/校验和
           每位 = 50us 低电平 + 26~28us(判 0) / 70us(判 1) 高电平
     本工程判 1 的阈值取 45us（代码里的 DHT_BIT_ONE_MIN_US）
     校验：前 4 字节之和的低 8 位 == 校验和；另外查量程（湿度 >100、温度 >60 报错）
     超时：所有等待阶段统一 120us；时间戳用 DWT->CYCCNT 数出来的
     整个"应答 + 40 位"过程约 4~5ms，期间 __disable_irq() 保证微秒级时序不被中断打断
     采样周期：CFG_DHT_PERIOD_MS = 2000ms（2.0s），远大于器件要求的 1s
     引脚方向：主机拉低时用【开漏输出(OD) + 上拉】，读的时候切【输入 + 上拉】，
               全程不会和从机对拉

6. 电压表 ADC1_IN5（PA5，板上 RV1 经 P10 跳线过来）
     ADC 时钟 = PCLK2 / 4 = 90MHz / 4 = 22.5MHz（F4 的 ADC 时钟上限 36MHz）
     采样时间 = 480 个 ADC 周期（取最长档：RV1 源阻抗高，给足建立时间）
     12 位、右对齐、软件触发、单次转换（不用 DMA，够用且简单）
     换算：V(mV) = raw x 3300 / 4095，先乘后除避免整数截断
     平均：BSP_ADC_ReadAverage(CFG_ADC_AVG_COUNT=16)，读失败的那几次不计入分母
     ⚠️ F4 的 ADC 没有用户可触发的自校准位（ADC_CR2 里只有 ADON/CONT/DMA/SWSTART…），
        所以本工程只开 ADON 并留 20us 稳定时间，读数噪声靠 16 次平均压制。

7. 界面字库 re-generate 机制（改了姓名/界面文字不用手工维护字库）
     tools/gen_font.py 用 Windows 自带的 simsun.ttc 生成点阵：
         小号：ASCII 12x24 + 汉字 24x24      大号：ASCII 16x32 + 汉字 32x32
     汉字来源 = 扫描 Inc/*.h 与 Src/*.c 的【字符串字面量】里的非 ASCII 字符
                （只扫字符串不扫注释）+ tools/ui_strings.txt 里的全部汉字
     打包：按行打包、每字节 MSB 在左、每行 ceil(宽/8) 字节
           （12 宽 2B/行、16 宽 2B/行、24 宽 3B/行、32 宽 4B/行）
     自动化：Makefile 里挂了依赖
                Inc/lcdfont.h: tools/gen_font.py tools/ui_strings.txt Inc/config.h Src/app.c
                $(BUILD)/bsp_font.o: Inc/lcdfont.h
             所以改 Inc/config.h 的姓名或 Src/app.c 的界面文字后直接 make 即可。
             命令前面加了 "-"，没装 Python/Pillow 的机器上这一步会失败但不中断构建
             （沿用旧字库，新加的字会显示成空白，需要手工执行：
                 py tools\gen_font.py Inc\lcdfont.h tools\ui_strings.txt .
              ）脚本是原子写：先写 .tmp 再 os.replace，中途失败不会弄坏已有的 lcdfont.h。

8. 任务周期一览（全部非阻塞，主循环按时间戳调度）
     任务            周期      说明
     --------------  --------  --------------------------------------------
     DHT11 采集      2000 ms   一次约 4~5ms，期间关中断
     ADC 电压采集     200 ms   16 次平均，每次 480 周期采样
     按键扫描          10 ms   20ms 消抖，只在按下边沿出事件
     界面局部刷新     200 ms   只重画变化的数值矩形
     报警红灯翻转     250 ms   半周期 -> 约 2Hz 闪烁
     摄像头帧超时     400 ms   一帧没搬完就判失联，连续 3 次判故障

----------------------------------------------------------------
 四、目录结构
----------------------------------------------------------------
Inc/
    pinmap.h              全工程引脚分配 + LCD 时序宏 + SDRAM 地址分配（唯一事实来源）
    config.h              姓名/学号/班级、任务周期、预警阈值与回差、字节序开关
    app.h                 应用层对外接口（App_Init / App_Task）
    main.h                HAL 总入口（包含 hal 头、Error_Handler 声明）
    gpio.h                基础 GPIO 初始化声明
    bsp_delay.h           微秒/毫秒延时（DWT 计数 + SysTick）
    bsp_led.h             板载双色 LED（低电平点亮）
    bsp_key.h             按键事件枚举（K0/K1/K2/UP）
    bsp_sdram.h           SDRAM 初始化/自检/刷新计数值查询
    bsp_lcd.h             LTDC 初始化、绘图、文本、图像搬运
    bsp_font.h            字库尺寸枚举与取字接口
    bsp_adc.h             ADC1 初始化与 原始值/毫伏/伏特 换算
    dht11.h               DHT11 状态枚举与数据结构
    ov5640.h              OV5640 状态枚举、输出尺寸宏、SCCB 读写接口
    bsp_dcmi.h            DCMI+DMA 采集接口（启动一帧/取一帧/查错/字节交换）
    stm32f4xx_it.h        中断服务函数声明
    stm32f4xx_hal_conf.h  HAL 模块裁剪配置
    lcdfont.h             【自动生成】点阵字库，不要手改

Src/
    main.c                时钟配置（180MHz + Over-Drive）+ 主循环只调 App_Task()
    gpio.c                4 个板载按键的 GPIO（其余引脚各自在 BSP 里配）
    app.c                 首页/摄像头页布局 + 局部刷新 + 预警状态机 + 任务调度
    bsp_delay.c           DWT 微秒延时 + 毫秒延时
    bsp_led.c             双色 LED 驱动（含 R62/R65 灌电流依据）
    bsp_key.c             20ms 消抖、只在按下边沿出事件
    bsp_sdram.c           FMC GPIO + SDRAM 上电序列 + 刷新率 + 读写自检
    bsp_lcd.c             PLLSAI 时钟、LTDC 时序/图层、绘图与文本、2 倍放大搬图
    bsp_font.c            字库查询（ASCII 单字节、汉字按 UTF-8 字节串匹配）
    bsp_adc.c             ADC1_IN5 单通道 + 16 次平均
    dht11.c               单总线时序实现（关中断 + DWT 计时 + 校验）
    ov5640.c              SCCB 软件模拟 + 硬件复位 + ID 校验 + RGB565/QVGA 寄存器表
    bsp_dcmi.c            DCMI + DMA2_Stream1 采集、帧完成回调、字节交换
    stm32f4xx_it.c        只有 SysTick / DCMI / DMA2_Stream1 三个中断
    stm32f4xx_hal_msp.c   只保留最基础的 HAL_MspInit（外设复用都写在各自 BSP）
    system_stm32f4xx.c    CMSIS 系统初始化（CubeIDE 模板）
    syscalls.c / sysmem.c newlib 桩函数（CubeIDE 模板）

tools/
    gen_font.py           字库生成脚本（构建时自动调用，见第三节第 7 条）
    ui_strings.txt        界面汉字的补充来源（源码扫描不到的字写在这里）
    make_wiring.py        接线示意图生成器（PIL 绘制，输出到 tools/wiring/）
    wiring/*.png          7 张接线示意图（总览/供电与DAP/RGB屏/摄像头/DHT11/RV1/按键）

WIRING.txt                接线指南与引脚解析：哪个模块接到哪个接口、怎么插、
                          上电顺序与故障排查、以及引脚推导方法与完整 AF 表
                          ——【上电前先看这份】

Drivers/                  STM32F4xx HAL 源码 + CMSIS（与实验一/二同一套）
Startup/                  startup_stm32f429zitx.s
STM32F429ZITX_FLASH.ld    链接脚本
Makefile                  make all / make clean
flash.bat                 一键烧录（自动挑最新的 elf，先清掉占用 DAP 的 openocd）
openocd_cmsis_dap.cfg     CubeIDE 调试用的 OpenOCD 配置（CMSIS-DAP）
.project / .cproject      CubeIDE 工程文件
SMART_HOME Debug.cfg/.launch  CubeIDE 调试启动配置

----------------------------------------------------------------
 五、编译与下载
----------------------------------------------------------------
【方式1：命令行】
    在本目录执行 make all，产物在 build\：
        SMART_HOME.elf / .hex / .bin
    然后双击 flash.bat 烧录。
    flash.bat 会去 Debug\ 和 build\ 里找【最新】的 SMART_HOME.elf，
    烧录前先 taskkill openocd.exe（IDE 上次失败留下的进程会占着 DAP），
    用的是 CubeIDE 自带的 openocd + interface/cmsis-dap.cfg + target/stm32f4x.cfg。

    ⚠️ Makefile 给链接器加了 --no-warn-rwx-segments：
       binutils >= 2.39 会对 CubeIDE 标准链接脚本的 FLASH 段报一条
       "LOAD segment with RWX permissions" 提示（对 STM32 无影响），
       加它是为了让编译输出保持 0 warning。用更老的工具链可以删掉这一段。

【方式2：CubeIDE】
    File -> Import -> General -> Existing Projects into Workspace
    目录选 D:\STM32Projects\SMART_HOME
    调试探头要选 OpenOCD + CMSIS-DAP（本机用的是正点原子 DAP，不是 ST-LINK），
    OpenOCD 脚本用工程里的 openocd_cmsis_dap.cfg。
    要改姓名/学号就改 Inc/config.h，然后重新编译，字库会自动重生。

----------------------------------------------------------------
 六、已知事项 / 待确认
----------------------------------------------------------------
1. 面板时序参数需按实际屏核对 ★最重要的一条
   pinmap.h 里的 HSW=48 / HBP=88 / HFP=40 / VSW=3 / VBP=32 / VFP=13 是
   按"典型 800x480 RGB 面板 + 30MHz 像素时钟（976x528, 58.2Hz）"填的。
   换屏、或者实验箱配的屏不是这一款时，这几个宏必须按屏的手册改；
   bsp_lcd.c 里那四位极性（HS/VS/DE 低有效、像素时钟上升沿采样）
   也要一起核对。花屏/偏移/图像滚动的第一嫌疑就是这一组，不是 SDRAM。
   本工程【没有在真机上把这组参数逐项量过】，只按典型值 + 手册对齐。

2. OV5640 的 RGB565 字节序可能要改（CFG_CAM_BYTE_SWAP）
   DCMI 走 8 位模式，一个像素两个字节，先到的是高字节。
   config.h 里默认 CFG_CAM_BYTE_SWAP = 0（不交换）。
   判定方法：如果预览【颜色明显偏色】（红蓝互换了那种），把它改成 1 再编；
   如果是【花屏/撕裂/错位】，那不是字节序问题，去查时序或 DMA，别改这个宏。

3. OV5640 默认 0x3035 = 0x41，帧率偏低，可以改 0x21 提升
   0x3035 的 bit[7:4] 是系统时钟分频，直接决定 PCLK：
        0x41 -> PLL 分频 /4 -> PCLK 约 14MHz（初始化表里的"稳妥档"）
        0x21 -> PLL 分频 /2 -> PCLK 约 28MHz（DCMI 上限 54MHz，还有余量）
   本工程默认走 14MHz。若实测预览帧率偏低、想更流畅，改 ov5640.c 的
   ov5640_rgb565_qvga[] 里那一条 {0x3035, 0x41} -> {0x3035, 0x21}
   （OV5640_SetOutputSize() 里切 VGA 用的就是 0x21，可以直接参考）。
   注意：PCLK 提上去以后，DCMI/DMA 的丢帧余量会变小，要重新看有没有撕裂。

4. DHT11 与 PCF8574 的 INT 共用 PB12（板级就是这样接的）
   PB12 在 IO 分配表里是 "IIC_INT / 1WIRE_DQ"：既接 PCF8574 的 INT 脚，
   又接单总线座子的 DQ 脚。PCF8574 的 INT 是【开漏输出】，本工程不用 PCF8574，
   它由上拉电阻保持高电平，不会干扰 DHT11 的时序。
   但如果将来要用 PCF8574 的中断（实验一/二里的蜂鸣器就在这颗芯片上），
   必须先保证它不会在 DHT11 通信期间把 PB12 拉低，否则会被当成从机应答，
   读出随机数据 —— 这两个功能不能同时用。

5. BKIN / 蜂鸣器本工程没有使用
   本工程一个定时器都没开（TIM1 刹车输入 BKIN、PCF8574 蜂鸣器都没有代码），
   所以"硬件急停"和"按键提示音"这两个实验二的功能在这里是不存在的。
   屏幕上的提示只有视觉反馈（双色灯 + 文字），没有声音。
   另外 Makefile 里仍然编着 stm32f4xx_hal_tim.c / hal_tim_ex.c，
   这是从模板沿用的，链接器 --gc-sections 会把没用到的代码丢掉，不影响大小。

6. LENC（镜头阴影校正）系数没有写
   ov5640.c 的寄存器表里【不含】0x5800~0x583d 那 62 个镜头阴影校正系数
   （OpenMV / esp32-camera 的官方默认组同样不含，芯片会用默认/OTP 值，
   一般出图是正常的）。如果整机上发现【四角偏暗或偏色】，再参考
   OmniVision《OV5640 DVP Application Note》把这一组补进第 8~12 块之间。
   代码里在那张表后面留了注释说明这件事，改的时候直接看那里的位置提示。

7. PB5 背光"高电平点亮"需要实测确认
   依据 IO 分配表，PB5 = LCD_BL（TFTLCD/RGBLCD 接口的背光控制脚），
   pinmap.h 里按 LCD_BL_ON = GPIO_PIN_SET（高电平点亮）写，bsp_lcd.c 里
   是先清帧缓冲、最后才 BSP_LCD_Backlight(1U)，避免上电白屏闪一下。
   但本工程【没有在真机上手量过这个脚的有效电平】：
   如果上电后屏是黑的、但用强光斜着能看到淡淡的图像，那就是背光电平反了，
   把 pinmap.h 的 LCD_BL_ON / LCD_BL_OFF 对调即可（一行改动）。

8. DHT11 采样周期是 2.0s
   生效值来自 Inc/config.h 的 CFG_DHT_PERIOD_MS = 2000U（2.0s），
   App_Task() 用 (now - t_dht) >= CFG_DHT_PERIOD_MS 判断，
   K2 还能随时手动触发一次（task_dht）。
   器件手册要求采样间隔 >= 1s，2.0s 留了 2 倍余量；源码注释与这里一致。

9. 摄像头帧率只统计、没有直接显示数值
   app.c 里已经把帧率算出来了（cam_fps，保存的是"帧率 x10"，
   每秒统计一次），但它目前只作为右侧状态栏的【重绘触发条件】，
   屏幕上显示的是"预览正常 / 预览异常"两态，没有把 fps 数字画出来。
   要显示具体帧率的话，在 ui_camera_side() 最后那一段加一行 DrawInt 即可
   （cam_fps / 10 取整、% 10 取小数）。

10. 整体声明：本工程【尚未在真机上完成全项验收】
    代码按 0 error 0 warning 的目标写完，静态推导（时钟、时序、刷新率、
    DMA 长度、字库覆盖）都逐项算过并对齐了手册，但下面这些只有上机才能确认：
        - 7 寸屏能不能正常出图（时序宏 + 极性 + 背光电平）
        - OV5640 的 SCCB 能不能一次配对（PB3/PB4 上拉、PA15 复位）
        - DHT11 读数是否稳定（单总线对时序很敏感，且和 PCF8574 共用 PB12）
        - RV1 电压读数与万用表是否吻合（480 周期采样 + 16 次平均够不够）
        - 反复切换页面时数据是否持续更新（要盯住 DHT/ADC 不因切页而停）
    验收时请按 TEST_PLAN.txt 逐项打勾，并把实测值填进记录表。

----------------------------------------------------------------
 七、修订记录（本轮自检发现并修正的地方）
----------------------------------------------------------------
    a) OV5640 寄存器表原先把"退出软件掉电 0x3008=0x02"放在中段，
       会导致配置期间就开始出图（可能配到一半就出帧）。已统一挪到表尾，
       并在后面加 {0xFFFF, 200} 给 AEC/AWB 收敛时间 —— 第一帧才不会是黑的。
    b) 补充说明：F4 的 ADC 没有用户自校准位，删掉了"等 CAL 清零"的死等逻辑，
       改成只开 ADON + 20us 稳定，靠 16 次平均压噪声，避免校准位不清零时卡死。
    c) DHT11 的引脚方向统一改成【开漏输出 + 上拉】/【输入 + 上拉】，
       不再用推挽输出，防止和从机对拉。
    d) 从 FAULT 恢复时清零三个越限标志，避免带着旧报警状态恢复。
    e) 摄像头取下一帧前强制 HAL_DCMI_Stop() + 清 OV_RRI 溢出标志，
       解决"帧从中间开始搬导致图像错位/撕裂"的问题。
    f) 局部刷新只重画变化的小矩形，并给每个数值单独记录上次画的内容，
       解决"整屏刷新会闪"和"重画留残影"两个问题。
================================================================
