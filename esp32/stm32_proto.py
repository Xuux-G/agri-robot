# stm32_proto.py — STM32 UART 帧协议解析器
# 帧格式: AA AA <block_num> FF FF (5 字节)

from machine import UART, Pin

class STM32Proto:
    def __init__(self, uart_id=2, tx=Pin(17), rx=Pin(18), baudrate=115200):
        self.uart = UART(uart_id, baudrate=baudrate, tx=tx, rx=rx)
        self.uart.init(baudrate=baudrate, bits=8, parity=None, stop=1, timeout=10)
        self._buf = bytearray()

    def check(self):
        while self.uart.any():
            b = self.uart.read(1)
            if b is None:
                continue
            self._buf.append(b[0])
            if len(self._buf) < 5:
                continue
            for i in range(len(self._buf) - 4):
                if (self._buf[i] == 0xAA and
                    self._buf[i+1] == 0xAA and
                    self._buf[i+3] == 0xFF and
                    self._buf[i+4] == 0xFF):
                    block = self._buf[i+2]
                    self._buf = bytearray()
                    if 1 <= block <= 6:
                        return block
                    return None
        if len(self._buf) > 64:
            self._buf = bytearray()
        return None