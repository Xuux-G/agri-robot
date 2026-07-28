# ESP32-S3 通信中枢

ESP32 是整个机器人的通信枢纽。它做三件事：把 STM32 读到的 NFC 地块数据通过 WiFi 发给基站、把基站返回的喷洒处方转发给 STM32、以及驱动 CI1302 语音模块播报操作提示。

固件用 MicroPython 编写，总共不到 500 行，结构很简单。

## 硬件接线

| 组件 | 接线 | 备注 |
|------|------|------|
| MCU | ESP32-S3-N16R8 (WROOM-1) | 16MB Flash, 8MB Octal PSRAM |
| STM32 | UART2: GPIO17(TX) / GPIO18(RX) | 9600 波特率 |
| CI1302 语音 | UART1: GPIO15(TX) / GPIO16(RX) | 115200 波特率 |
| WS2812B RGB | GPIO48 | 状态指示灯 |
| PC (REPL) | UART0: GPIO43(TX) / GPIO44(RX) | CH343 USB 转串口, 115200 波特率 |

## 引脚一览

| GPIO | 功能 | 协议 |
|------|------|------|
| 15 / 16 | 语音模块 TX/RX | UART1 @ 115200 |
| 17 / 18 | STM32 TX/RX | UART2 @ 9600 |
| 43 / 44 | REPL 控制台 | UART0 @ 115200 |
| 48 | WS2812B RGB LED | NeoPixel |

## 烧录步骤

### 1. 烧录 MicroPython 固件

从 [micropython.org](https://micropython.org/download/ESP32_GENERIC_S3/) 下载 `ESP32_GENERIC_S3-SPIRAM_OCT` 版本。

```bash
pip install esptool

# 进入下载模式：按住 BOOT → 按一下 RST → 松开 BOOT
esptool --port COM3 erase_flash
esptool --port COM3 --chip esp32s3 write_flash -z 0x0 firmware.bin
```

### 2. 配置 WiFi 和基站地址

把 `config.example.py` 复制一份为 `config.py`，填上自己的 WiFi 密码和基站 IP 地址：

```python
WIFI_SSID = "你的WiFi名"
WIFI_PASS = "你的WiFi密码"
BASE_STATION_HOST = "基站的IP地址"
```

### 3. 上传代码文件

```bash
cd esp32
mpremote connect COM3 fs cp main.py :main.py
mpremote connect COM3 fs cp boot.py :boot.py
mpremote connect COM3 fs cp config.py :config.py
mpremote connect COM3 fs cp stm32_proto.py :stm32_proto.py
mpremote connect COM3 fs cp voice_module.py :voice_module.py
mpremote connect COM3 fs cp base_station.py :base_station.py
```

按 RST 键重启，REPL 输出 `WiFi OK` + `ready` 就说明运行正常了。

## 通信协议

### STM32 → ESP32（NFC 帧）
```
AA AA <地块编号> FF FF    (5 字节, 9600 波特率)
地块编号: 01-06
```

### ESP32 → STM32（处方帧）
```
AA AA <地块> <病虫害代码> FF FF    (6 字节, 9600 波特率)
病虫害代码: 1=杀菌剂 2=杀虫剂 3=杀螨剂 4=无病虫害
```

### ESP32 → CI1302 语音模块
```
AA 55 <指令> 00 FB    (5 字节, 115200 波特率)
指令值: 06-0B 地块播报, 0C-15 作物播报, 16-1D 病虫害播报
```

### ESP32 → 基站（HTTP）

事件数据以 JSON 格式 POST 到 `/api/device/events`：

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

处方指令通过 `GET /api/device/command?robot_id=agricultural robot` 轮询获取。

## 文件说明

| 文件 | 功能 |
|------|------|
| `main.py` | 主循环：WiFi 连接、NFC 帧解析、HTTP 上报、指令轮询 |
| `boot.py` | 开机初始化 |
| `stm32_proto.py` | STM32 UART 帧解析与指令编码 |
| `voice_module.py` | CI1302 语音指令分发 |
| `base_station.py` | 基站 API 的 HTTP 客户端 |
| `pin_check.py` | GPIO 引脚诊断工具 |
| `flash_esp32.bat` | 一键烧录 + 上传脚本 |

`pin_check.py` 可以单独跑，排查接线问题。连上 REPL 后执行：

```python
import pin_check
pin_check.scan()
```
