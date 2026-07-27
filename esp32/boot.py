# boot.py — 启动时最先执行，完成基础初始化
# MicroPython 上电后自动运行此文件，然后运行 main.py

import machine
import time

# 板载 WS2812B RGB 在 GPIO48
# 启动时先关闭，等 WiFi 连接成功后再亮
try:
    import neopixel
    _rgb = neopixel.NeoPixel(machine.Pin(48), 1)
    _rgb[0] = (0, 0, 0)
    _rgb.write()
except Exception:
    pass  # 非关键外设，失败不阻塞启动

print("[boot] ESP32-S3 boot complete, entering main.py")
