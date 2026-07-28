# STM32H7 主控制器

机器人的底层大脑。负责电机驱动、舵机控制、三通道蠕动泵配药、NFC 读卡、超声波避障、IMU 姿态感知，以及通过 UART 和 ESP32 / MaixCam2 通信。

## 硬件

- **MCU**: STM32H7B0VBTx (ARM Cortex-M7, 280MHz)
- **工具链**: GCC ARM Embedded (`arm-none-eabi-gcc`), CMake + Ninja
- **外设**: PWM 电机驱动 ×4, 舵机 ×2, 蠕动泵 ×3, PN532 NFC, HC-SR04, JY61 IMU

## 外设驱动

| 驱动文件 | 控制对象 |
|----------|----------|
| `Motor.c/h` | 底盘直流电机 PWM + 方向控制 |
| `servo.c/h` | 摄像头云台舵机 |
| `spray_ctrl.c/h` | 三通道蠕动泵配药 |
| `nfc_handler.c/h`, `pn532.c/h`, `pn532_2.c/h` | PN532 NFC 读写（地块识别）|
| `hcsr04.c/h` | HC-SR04 超声波测距 |
| `jy61.c/h` | JY61 六轴姿态传感器 |
| `camera.c/h` | 与 MaixCam2 的 UART 指令协议 |
| `auto_drive.c/h` | 自主循线 + 树间导航 |
| `emm_v5.c/h` | 电机编码器反馈 |
| `pstwo.c/h` | PS2 手柄遥控（手动备选）|
| `gpio.c/h`, `usart.c/h`, `tim.c/h` | HAL 层外设初始化 |

## 编译

### 环境

- ARM GCC 工具链 (`arm-none-eabi-gcc`)
- CMake ≥ 3.22
- Ninja（或者 Make）

### 编译命令

```bash
cd stm32
cmake --preset default
cmake --build build/Debug
```

产物在 `build/Debug/Sprayer.elf`。

### 烧录

用 STM32CubeProgrammer、OpenOCD 或调试器：

```bash
STM32_Programmer_CLI -c port=SWD -w build/Debug/Sprayer.elf -v
```

## 项目结构

```
stm32/
├── Core/
│   ├── Inc/          # 应用层头文件
│   └── Src/          # 应用层代码 + HAL MSP + ISR 中断处理
├── Drivers/
│   ├── CMSIS/        # ARM CMSIS
│   └── STM32H7xx_HAL_Driver/  # STM32 HAL 库
├── cmake/            # arm-none-eabi-gcc 工具链文件
├── Sprayer.ioc       # CubeMX 项目文件
├── CMakeLists.txt
├── CMakePresets.json
├── startup_stm32h7b0xx.s
└── STM32H7B0VBTx_FLASH.ld
```

要修改引脚或外设，打开 `Sprayer.ioc` 在 CubeMX 里改，重新生成代码后手动合并到 `Core/Src/` 和 `Core/Inc/`。

## UART 分配

| UART | 对接设备 | 波特率 | 用途 |
|------|----------|--------|------|
| USART2 | ESP32 | 9600 | NFC 帧上行 + 处方指令下行 |
| USART3 | MaixCam2 | 115200 | 视觉引导运动数据包 |
| UART7 | JY61 IMU | 115200 | 姿态数据 |
| USART1 | 调试 / PS2 | — | 串口终端 / 手柄输入 |

## 运行逻辑

- **NFC 唤醒**：上电后等待 NFC 标签读取，读到后进入自主模式
- **循线行驶**：`auto_drive.c` 用超声波和编码器反馈实现作物行间循线
- **树间切换**：当前树处理完毕后自动前进到下一棵，触发新的 NFC + 摄像头流程
- **手动接管**：PS2 手柄随时可以接管，用于调试或遥控操作
