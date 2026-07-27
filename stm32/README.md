# STM32H7 Main Controller

Motion control and sensor integration firmware for the agricultural spraying robot. Built on STM32CubeMX with HAL drivers and CMake build system.

## Hardware

- **MCU**: STM32H7B0VBTx (ARM Cortex-M7, 280 MHz)
- **Board**: Custom carrier with peristaltic pump drivers, servo interfaces, and sensor ports
- **Toolchain**: GCC ARM Embedded (`arm-none-eabi-gcc`), CMake + Ninja

## Peripheral Map

| Peripheral | Driver File | Function |
|-----------|-------------|----------|
| Motor Control | `Motor.c/h` | DC motor PWM + direction for chassis drive |
| Servo Control | `servo.c/h` | PWM servo for camera arm positioning |
| Spray Control | `spray_ctrl.c/h` | 3-channel peristaltic pump dosing |
| NFC Reader | `nfc_handler.c/h`, `pn532.c/h`, `pn532_2.c/h` | PN532 NFC tag read/write for plot identification |
| Ultrasonic | `hcsr04.c/h` | HC-SR04 distance sensing |
| IMU | `jy61.c/h` | JY61 6-axis attitude sensor |
| Camera Interface | `camera.c/h` | UART command protocol to MaixCam2 |
| Auto Drive | `auto_drive.c/h` | Autonomous ridge-following and tree-to-tree navigation |
| Encoder | `emm_v5.c/h` | Motor encoder feedback |
| PS2 Controller | `pstwo.c/h` | Manual remote control fallback |
| GPIO / UART / TIM | `gpio.c/h`, `usart.c/h`, `tim.c/h` | HAL-level peripheral initialization |

## Build

### Prerequisites

- ARM GCC toolchain (`arm-none-eabi-gcc`)
- CMake ≥ 3.22
- Ninja (or Make)

### Compile

```bash
cd stm32
cmake --preset default
cmake --build build/Debug
```

The ELF binary is at `build/Debug/Sprayer.elf`.

### Flash

Use STM32CubeProgrammer, OpenOCD, or your preferred debug probe:

```bash
STM32_Programmer_CLI -c port=SWD -w build/Debug/Sprayer.elf -v
```

## Project Configuration

The project was generated with STM32CubeMX. To modify pin assignments or enable/disable peripherals:

1. Open `Sprayer.ioc` in STM32CubeMX
2. Make changes and regenerate code
3. Merge generated files with existing application code in `Core/Src/` and `Core/Inc/`

## Source Structure

```
stm32/
├── Core/
│   ├── Inc/          # Application headers
│   └── Src/          # Application source + HAL MSP + ISR handlers
├── Drivers/
│   ├── CMSIS/        # ARM CMSIS core
│   └── STM32H7xx_HAL_Driver/  # STM32 HAL library
├── cmake/            # Toolchain file for arm-none-eabi-gcc
├── Sprayer.ioc       # CubeMX project
├── CMakeLists.txt    # Top-level CMake
├── CMakePresets.json # Build presets
├── startup_stm32h7b0xx.s  # Startup assembly
└── STM32H7B0VBTx_FLASH.ld  # Linker script
```

## UART Communication

| UART | Partner | Baud | Protocol |
|------|---------|------|----------|
| USART2 | ESP32 | 9600 | NFC frames + prescription commands |
| USART3 | MaixCam2 | 115200 | Visual servoing motion packets |
| UART7 | JY61 IMU | 115200 | Attitude data |
| USART1 | Debug / PS2 | — | Terminal output / controller input |

## Key Behaviors

- **NFC Wake-up**: On power-up, the robot waits for an NFC tag read before entering autonomous mode
- **Ridge Following**: `auto_drive.c` implements line-following between crop rows using ultrasonic and encoder feedback
- **Tree-to-Tree**: After completing one tree, the robot advances to the next and triggers a new NFC + camera cycle
- **Manual Override**: PS2 controller can take over at any time for debugging or teleoperation
