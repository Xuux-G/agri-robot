# STM32H7 主控制器

机器人的底层控制核心。负责底盘电机驱动、舵机云台控制、三通道蠕动泵配药、NFC 地块读卡、超声波避障、IMU 姿态感知，并通过 UART 和 ESP32 / MaixCam2 通信。

## 硬件

- **主控**: STM32H7B0VBTx (ARM Cortex-M7, 280MHz)
- **开发环境**: GCC ARM Embedded (`arm-none-eabi-gcc`), CMake + Ninja
- **主要外设**: PWM 直流电机 ×4, 舵机 ×2, 蠕动泵 ×3, PN532 NFC 模块, HC-SR04 超声波, JY61 六轴 IMU

## 外设驱动一览

| 驱动文件 | 控制对象 |
|----------|----------|
| `Motor.c/h` | 底盘直流电机 PWM + 方向控制（四轮差速驱动）|
| `servo.c/h` | 摄像头云台舵机角度控制 |
| `spray_ctrl.c/h` | 三通道蠕动泵，按配比独立控制流量 |
| `nfc_handler.c/h`, `pn532.c/h`, `pn532_2.c/h` | PN532 NFC 读写，识别地块标签 |
| `hcsr04.c/h` | HC-SR04 超声波测距 |
| `jy61.c/h` | JY61 六轴 IMU 姿态解算 |
| `camera.c/h` | 与 MaixCam2 的 UART 指令交互协议 |
| `auto_drive.c/h` | 自动循线行驶 + 树间切换导航 |
| `emm_v5.c/h` | 电机编码器里程计反馈 |
| `pstwo.c/h` | PS2 无线手柄遥控（手动模式备用）|
| `gpio.c/h`, `usart.c/h`, `tim.c/h` | HAL 库层外设初始化封装 |

## 编译

### 准备环境

- ARM GCC 交叉编译工具链 (`arm-none-eabi-gcc`)
- CMake ≥ 3.22
- Ninja（或者用 Make 也行）

### 编译命令

```bash
cd stm32
cmake --preset default
cmake --build build/Debug
```

编译产物 `build/Debug/Sprayer.elf` 就是可以烧录的固件。

### 烧录到芯片

用 STM32CubeProgrammer、OpenOCD 或者 J-Link / ST-Link 调试器都可以：

```bash
STM32_Programmer_CLI -c port=SWD -w build/Debug/Sprayer.elf -v
```

## 项目目录结构

```
stm32/
├── Core/
│   ├── Inc/          # 应用层所有头文件
│   └── Src/          # 应用层代码 + HAL MSP 配置 + ISR 中断处理
├── Drivers/
│   ├── CMSIS/        # ARM CMSIS 核心文件
│   └── STM32H7xx_HAL_Driver/  # ST 官方 HAL 驱动库
├── cmake/            # 交叉编译工具链配置文件
├── Sprayer.ioc       # STM32CubeMX 工程文件
├── CMakeLists.txt    # 顶层 CMake 构建脚本
├── CMakePresets.json # CMake 构建预设
├── startup_stm32h7b0xx.s  # 启动汇编文件
└── STM32H7B0VBTx_FLASH.ld  # 链接脚本
```

如果需要改引脚分配或者开关外设，打开 `Sprayer.ioc` 用 CubeMX 修改，重新生成代码后手动把改动合并到 `Core/Src/` 和 `Core/Inc/` 里就行。

## UART 通信分配

| UART | 对接设备 | 波特率 | 用途 |
|------|----------|--------|------|
| USART2 | ESP32 | 9600 | NFC 数据上行 + 喷洒处方下行 |
| USART3 | MaixCam2 | 115200 | 视觉引导运动数据包接收 |
| UART7 | JY61 IMU | 115200 | 姿态数据读取 |
| USART1 | 调试 / PS2 手柄 | — | 串口终端打印 / 手柄遥控 |

## 运行逻辑

- **NFC 唤醒**：上电后等待读取 NFC 地块标签，读到后自动进入自主作业模式
- **循线行驶**：`auto_drive.c` 用超声波和编码器反馈实现作物行间循线，不需要预埋导轨
- **树间切换**：当前树喷洒完成后，自动前进到下一棵树，触发新一轮 NFC 验证 + 摄像头检测
- **手动接管**：PS2 手柄随时可以切手动模式，方便调试或者遥控操作
