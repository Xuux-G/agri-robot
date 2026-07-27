# ESP32-S3 Connectivity Hub

MicroPython firmware for the ESP32-S3 that bridges STM32, MaixCam2, CI1302 voice module, and the base station over WiFi.

## Hardware

| Component | Connection | Notes |
|-----------|-----------|-------|
| MCU | ESP32-S3-N16R8 (WROOM-1) | 16MB Flash, 8MB Octal PSRAM |
| STM32 | UART2: GPIO17(TX) / GPIO18(RX) | 9600 baud |
| CI1302 Voice | UART1: GPIO4(TX) / GPIO5(RX) | 115200 baud |
| WS2812B RGB | GPIO48 | Status indicator |
| PC (REPL) | UART0: GPIO43(TX) / GPIO44(RX) | CH343 USB-UART, 115200 baud |

## Pin Reference

All GPIO pins support remapping via ESP32-S3's GPIO Matrix. The table below lists only the assignments used in this project:

| GPIO | Function | Protocol |
|------|----------|----------|
| 4 / 5 | Voice module TX/RX | UART1 @ 115200 |
| 17 / 18 | STM32 TX/RX | UART2 @ 9600 |
| 43 / 44 | REPL console | UART0 @ 115200 |
| 48 | WS2812B RGB LED | NeoPixel |

## Firmware Setup

### 1. Flash MicroPython

Download the `ESP32_GENERIC_S3-SPIRAM_OCT` firmware from [micropython.org](https://micropython.org/download/ESP32_GENERIC_S3/).

```bash
pip install esptool

# Enter download mode: hold BOOT -> press RST -> release BOOT
esptool --port COM3 erase_flash
esptool --port COM3 --chip esp32s3 write_flash -z 0x0 firmware.bin
```

### 2. Configure

Copy `config.example.py` to `config.py` and fill in your WiFi credentials and base station IP:

```python
WIFI_SSID = "your_wifi"
WIFI_PASS = "your_password"
BASE_STATION_HOST = "your_base_station_ip"
```

### 3. Upload Code

```bash
cd esp32
mpremote connect COM3 fs cp main.py :main.py
mpremote connect COM3 fs cp boot.py :boot.py
mpremote connect COM3 fs cp config.py :config.py
mpremote connect COM3 fs cp stm32_proto.py :stm32_proto.py
mpremote connect COM3 fs cp voice_module.py :voice_module.py
mpremote connect COM3 fs cp base_station.py :base_station.py
```

Press RST to reboot. The REPL should show `WiFi OK` + `ready`.

## Communication Protocol

### STM32 -> ESP32 (NFC frame)
```
AA AA <block_num> FF FF    (5 bytes, 9600 baud)
block_num: 01-06
```

### ESP32 -> STM32 (prescription frame)
```
AA AA <block> <pest_code> FF FF    (6 bytes, 9600 baud)
pest_code: 1=fungicide 2=bactericide 3=insecticide 4=no pest
```

### ESP32 -> CI1302 Voice
```
AA 55 <cmd> 00 FB    (5 bytes, 115200 baud)
cmd values: 06-0B = block announcement, 0C-15 = crop ID, 16-1D = pest treatment
```

### ESP32 -> Base Station (HTTP)

Events are POSTed as JSON to `/api/device/events`:

```json
{
  "event_id": "CAR01-0001",
  "robot_id": "agricultural robot",
  "block_id": "A-01",
  "pest_type": "aphid",
  "action_type": "spray",
  "status": "running",
  "temperature": 25.6,
  "humidity": 61.2
}
```

Prescriptions are polled via `GET /api/device/command?robot_id=agricultural robot`.

## File Overview

| File | Purpose |
|------|---------|
| `main.py` | Main loop: WiFi connect, NFC → HTTP upload, command polling |
| `boot.py` | Boot-time initialization |
| `stm32_proto.py` | STM32 UART frame parsing and command encoding |
| `voice_module.py` | CI1302 voice command dispatch |
| `base_station.py` | HTTP client for base station API |
| `pin_check.py` | GPIO pin diagnostic utility |
| `flash_esp32.bat` | One-click firmware flash and upload script |

## Utility

`pin_check.py` can be run independently to verify GPIO wiring. Connect to REPL and execute:

```python
import pin_check
pin_check.scan()
```
