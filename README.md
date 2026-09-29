# 微处理器与机器人 · 实验

深圳大学 计算机与软件学院《微处理器与机器人》课程的实验工程集合，平台为
**正点原子阿波罗 V2 STM32F429（STM32F429IGTx / LQFP176）+ DNF429-EB 嵌入式学习实验箱**。

仓库根目录（`D:\STM32Projects`）就是工程根：**每个实验一个目录**，平铺在同级；
实验报告、临时工作区、备份目录不纳入本仓库（见 `.gitignore`）。
每个工程都带有 `Readme.txt`（设计说明与参数推导）、`WIRING.txt`（接线指南与引脚推导方法）、
`TEST_PLAN.txt`（验收步骤与记录表）。

## 目录结构

| 目录 | 实验 | 内容 | 主要外设 / 知识点 |
|---|---|---|---|
| [`LED_BEEP`](LED_BEEP) | 实验一 | 跑马灯与蜂鸣器 | GPIO、软件 IIC（PCF8574）、软件 PWM 呼吸灯、按键与灯效状态机 |
| [`TIMER_LAB`](TIMER_LAB) | 实验二 | 定时器综合实验（5 个子实验） | TIM6 毫秒基准、TIM3 三路 PWM 呼吸灯、TIM4 输出 + TIM3 输入捕获自测仪、TIM1 的 RCR/OPM/MOE/BKIN、SSD1306 OLED 菜单 |
| [`SMART_HOME`](SMART_HOME) | 实验三 | 智能家居中控终端 | DHT11 单总线、ADC 电压表（RV1→PA5）、7 寸 800×480 RGB 屏（LTDC + 外部 SDRAM 帧缓冲）、OV5640（DCMI + DMA）实时预览、阈值预警状态机 |
| [`FACE_LAB`](FACE_LAB) | 实验四 | 人脸录入与识别 | OV5640 取景拍照、48×48 人脸库持久化到板载 W25Q256（含 CRC 校验与断电重载）、MSE 模板匹配 / 3×3 LBP + 4×4 分区直方图 / 颜色直方图对照 / SVD-PCA 特征脸四种方式、验证集阈值标定与开放集判定 |
| [`guide`](guide) | — | 实验引导资料 | 《STM32F429 阿波罗开发指南》、《阿波罗V2 硬件参考手册》、**开发板 IO 引脚分配表**、CubeIDE 使用教程、实验箱用户手册、面包板实验手册等 |

## 硬件平台

- **开发板**：正点原子阿波罗 V2 STM32F429（STM32F429IGTx）
- **实验箱**：DNF429-EB
- **时钟**：HSE 25 MHz → PLL → SYSCLK 180 MHz（HCLK 180 / APB1 45 / APB2 90 MHz）
- **常用外设**：7 寸 800×480 RGB 屏（LTDC + W9825G6KH SDRAM）、ATK-OV5640 摄像头（P6，DCMI+DMA）、
  DHT11 温湿度、板载 W25Q256 SPI NOR Flash、板载双色 LED（PB0 绿 DS1 / PB1 红 DS0，低电平点亮）、
  4 个按键（KEY0=PH3 / KEY1=PH2 / KEY2=PC13 / WK_UP=PA0）
- **调试**：SWD（PA13/PA14），用实验箱自带的 DAP 仿真器（CMSIS-DAP，不是 ST-LINK）

## 编译与下载

**命令行（Makefile）**

```bash
cd LED_BEEP        # 或 TIMER_LAB / SMART_HOME / FACE_LAB
make all           # 产物在 build/：*.elf / *.hex / *.bin
```

需要 `arm-none-eabi-gcc` 与 `make`（也可直接使用 STM32CubeIDE 自带的工具链）。

**STM32CubeIDE**

`File → Import → General → Existing Projects into Workspace`，选择对应工程目录即可。
调试探头选 `OpenOCD + CMSIS-DAP`，脚本用工程里的 `openocd_cmsis_dap.cfg`。

**一键下载**

双击工程目录下的 `flash.bat`：自动挑最新的 `.elf`，用 OpenOCD + CMSIS-DAP 烧录并复位。

## 工程规范

每个工程都按同样的方式组织：

```
Src/            应用层与板级驱动（app.c 状态机、bsp_*.c 外设、算法等）
Inc/            pinmap.h（引脚唯一事实来源）、config.h（学生信息与可调参数）、各模块头文件
tools/          gen_font.py（由源码字符串自动生成点阵字库）、界面仿真、接线图生成等脚本
Drivers/        ST 官方 STM32F4xx HAL 驱动与 CMSIS
Makefile        命令行构建；与 CubeIDE 工程配置并存
Readme/WIRING/TEST_PLAN.txt   设计说明、接线指南、验收步骤
```

约定：
- 引脚只允许引用 `Inc/pinmap.h` 中的宏，不在代码里写裸端口名，换板只改一处；
- 所有延时/采样/界面刷新都按"时间戳 + 周期"非阻塞调度，主循环里不出现等待；
- 中文界面文字来自源码字符串字面量，构建时自动重新生成字库，不需要手工维护。

## 新增实验（例如实验五）怎么做

工程目录平铺在仓库根，忽略规则是**按模式**写的，所以常规新增试验**不需要改任何配置**：

1. **新建工程目录**：放在仓库根，与 `LED_BEEP`、`TIMER_LAB` 同级，例如 `MOTOR_LAB/`。
   它不在任何忽略规则里，`git add MOTOR_LAB` 就能直接纳入版本管理。
2. **实验报告目录**：如果也放在这里，命名成 `experiment5/`（或 `experiment5_xxx/`）。
   `.gitignore` 第 1 节的 `/experiment*/` 会自动忽略它，**以后写新报告都不用再改配置**。
3. **沿用工程规范**：`Inc/pinmap.h`（引脚唯一事实来源）、`Inc/config.h`（学生信息与参数）、
   `tools/gen_font.py`（自动字库）、`Makefile`（命令行构建）、
   `Readme.txt` / `WIRING.txt` / `TEST_PLAN.txt`（设计说明、接线、验收）。
4. **提交**：

   ```bash
   cd /d D:\STM32Projects
   git status            # 先看一眼：应该只有新实验的源码，没有 build/ Debug/ 产物
   git add MOTOR_LAB     # 也可以 git add -A，忽略规则已经帮你挡掉噪声
   git commit -m "实验五 电机控制"
   git push
   ```

5. **什么时候才需要动 `.gitignore`**：
   - 新工程带来**新的可再生成产物**（Keil 的 `Objects/`、`Listings/`、`*.uvguix.*`，
     或别的 IDE 的输出目录）→ 往第 2 节"编译构建产物"追加一行；
   - 新增**临时/备份目录** → 追加到第 1 节；
   - 只是"源码 + 报告目录"→ **不需要改**。

## 推送到 GitHub

远程仓库：<https://github.com/leonal53589/Microprocessors-and-Robot>

```bash
cd /d D:\STM32Projects
git remote -v                 # 已配置好 origin，无需重复添加
git push -u origin main
```

- 本仓库首次推送约 **51 MiB**（其中大部分是 `guide/` 里的参考资料），耗时取决于上行带宽；
  中途断开直接重跑 `git push` 即可（已把 `http.version` 设为 HTTP/1.1、`http.postBuffer` 调小走分块上传）。
- 认证用 Git Credential Manager（`credential.helper=manager`），首次会弹浏览器授权，跟一次即可；
  若直连 GitHub 不稳定，可给 git 配代理：

  ```bash
  git config --global http.proxy http://127.0.0.1:7890    # 换成自己的代理端口
  ```

## 关于本仓库的内容取舍

- **不包含**：实验报告（LaTeX）、临时工作区 `ws/`、CubeMX 备份 `_ioc_backup/`；
- **不包含**：编译构建产物（`build/`、`Debug/`、`Release/`、`*.o/*.d/*.su/*.cyclo/*.map/*.list`），
  克隆后重新编译即可；但 `versions/` 版本快照里保留的 `*.elf/*.hex/*.bin` 是刻意留下的可烧录产物；
- **不包含**：ST 官方 `*.chm` 器件手册与 CMSIS 的 `DSP/` 例程、`Documentation/`、`DAP/`、`NN/`、`RTOS/` 等厂商资料
  （体积大且编译不引用；编译真正需要的 `CMSIS/Include` 与 `CMSIS/Device/ST/STM32F4xx/Include` 都在仓库里）。
  这些资料可从 ST 官网或原厂开发板资料包获取。
- **不包含**：`guide/` 里体积过大的那份通用手册《STM32F429 阿波罗开发指南》（61.6 MB）——
  它是可以在正点原子资料包中重新下载的通用文档，为了让仓库能被顺利克隆与推送而排除在版本管理之外；
  `guide/` 里其余参考资料（硬件参考手册、CubeIDE 使用教程、**开发板 IO 引脚分配表**、
  实验箱用户手册、面包板实验手册等）都正常纳入仓库。

## 注意

- 引脚与接线以《阿波罗V2 STM32F429 开发板 IO 引脚分配表》（见 `guide/`）为准；
  实验箱的实际接线请对照各工程的 `WIRING.txt`。
- 少数参数需要在真机上按实际硬件微调（RGB 屏时序与极性、摄像头 RGB565 字节序、
  各识别算法的拒识阈值、DHT11 采样周期等），各工程的 `Readme.txt` 里都有
  「已知事项 / 待上板确认」清单，请先阅读再上电调试。
