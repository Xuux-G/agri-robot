# main.py - ESP32-S3 hub: STM32 TTL + voice + base station sync
from machine import UART, Pin
from time import sleep_ms, time
import network

try:
    import usocket as socket
except ImportError:
    import socket

try:
    import ujson as json
except ImportError:
    import json

from config import (
    BASE_STATION_HOST,
    BASE_STATION_PORT,
    DEVICE_ID,
    STM32_BAUD,
    STM32_RX,
    STM32_TX,
    VOICE_TX,
    VOICE_RX,
    WIFI_PASS,
    WIFI_SSID,
)
from voice_module import VoiceModule

HOST = BASE_STATION_HOST
PORT = BASE_STATION_PORT
POLL_MS = 1000
CROP_VOICE = {1: 0x12, 2: 0x15, 3: 0x11, 4: 0x0D, 5: 0x10, 6: 0x0C}
# 临时关闭按区块固定诊断播报，测试时仅按识别出的病虫害播报
TTL_SIM_VOICE_MODE = False

stm32 = UART(2, STM32_BAUD, tx=Pin(STM32_TX), rx=Pin(STM32_RX), timeout=10)
vm = VoiceModule(uart_id=1, tx=Pin(VOICE_TX), rx=Pin(VOICE_RX))
wifi = network.WLAN(network.STA_IF)

buf = bytearray()
last_block = 0
last_poll = 0
pending_events = []
voice_queue = []
voice_cooldown_until = 0
recent_pest_voice = {}
last_completed_block = 0

VOICE_GAP_MS = 2400              # 默认语音间隔（无 BUSY 脚，必须大于单条最长语音）
VOICE_START_DELAY_MS = 60       # 首条语音启动延迟
BLOCK_VOICE_GAP_MS = 2700       # 区块检测语音后的间隔（作物语音紧跟其后）
DIAGNOSIS_VOICE_GAP_MS = 2200   # 病虫害诊断序列间隔
VOICE_HARD_COOLDOWN_MS = 3200   # 按实际播放时间强制冷却，避免后续语音顶掉当前语音
PEST_VOICE_COOLDOWN_MS = 60 * 1000

DIAGNOSIS_SEQUENCES = {
    1: ['DETECT_CAIQINGCHONG', 'AI_ANALYSIS', 'KILL_MITE'],
    2: ['DETECT_BAIFEN', 'AI_ANALYSIS', 'TREAT_FUNGUS'],
    3: ['DETECT_HEBAN_CAIQINGCHONG', 'AI_ANALYSIS', 'TREAT_HEBAN_CAIQINGCHONG'],
    4: ['DETECT_WONIU', 'AI_ANALYSIS', 'TREAT_SNAIL'],
    5: ['NO_PEST_FOUND', 'HISTORY_JUDGE', 'NO_PEST'],
    6: ['DETECT_HEBAN', 'AI_ANALYSIS', 'MANUAL_RECHECK'],
}

TTL_SIM_DIAGNOSIS_MAP = {
    0x07: 1,
    0x08: 2,
    0x09: 3,
    0x0A: 4,
    0x0B: 5,
    0x0C: 6,
}

PEST_DIAGNOSIS_SEQUENCES = {
    '菜青虫': ['DETECT_CAIQINGCHONG', 'AI_ANALYSIS', 'KILL_MITE'],
    '白粉病': ['DETECT_BAIFEN', 'AI_ANALYSIS', 'TREAT_FUNGUS'],
    '褐斑病+菜青虫': ['DETECT_HEBAN_CAIQINGCHONG', 'AI_ANALYSIS', 'TREAT_HEBAN_CAIQINGCHONG'],
    '蜗牛': ['DETECT_WONIU', 'AI_ANALYSIS', 'TREAT_SNAIL'],
    '正常': ['NO_PEST_FOUND', 'HISTORY_JUDGE', 'NO_PEST'],
    'none': ['NO_PEST_FOUND', 'HISTORY_JUDGE', 'NO_PEST'],
    '褐斑病': ['DETECT_HEBAN', 'AI_ANALYSIS', 'TREAT_FUNGUS'],
}


def now_ms():
    return int(time() * 1000)


def http_request(method, path, body=None):
    try:
        addr = socket.getaddrinfo(HOST, PORT)[0][-1]
        s = socket.socket()
        s.settimeout(3)
        s.connect(addr)
        if body is None:
            req = '%s %s HTTP/1.1\r\nHost:%s\r\nConnection: close\r\n\r\n' % (method, path, HOST)
        else:
            req = '%s %s HTTP/1.1\r\nHost:%s\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s' % (
                method,
                path,
                HOST,
                len(body),
                body,
            )
        s.send(req)
        data = b''
        while True:
            chunk = s.recv(512)
            if not chunk:
                break
            data += chunk
        s.close()
        return data
    except Exception as exc:
        print(method, 'err:', exc)
        return None


def post_event(body):
    return http_request('POST', '/api/device/events', body)


def get_command():
    return http_request('GET', '/api/device/command?robot_id=%s' % DEVICE_ID)


def queue_voice(cmd_id, gap_ms=VOICE_GAP_MS, delay_ms=0):
    global voice_cooldown_until
    start_at = max(now_ms() + delay_ms, voice_cooldown_until)
    voice_queue.append((start_at, cmd_id))
    voice_cooldown_until = start_at + gap_ms


def queue_voice_sequence(cmd_ids, gap_ms=VOICE_GAP_MS, delay_ms=0):
    for index, cmd_id in enumerate(cmd_ids):
        queue_voice(cmd_id, gap_ms=gap_ms, delay_ms=delay_ms if index == 0 else 0)


def process_voice_queue():
    global voice_cooldown_until
    if not voice_queue:
        return
    start_at, cmd_id = voice_queue[0]
    now = now_ms()
    if now < start_at:
        return
    vm.play(cmd_id)
    voice_queue.pop(0)
    voice_cooldown_until = max(voice_cooldown_until, now + VOICE_HARD_COOLDOWN_MS)


def queue_block_diagnosis(block):
    keys = DIAGNOSIS_SEQUENCES.get(block, [])
    if keys:
        queue_voice_sequence(
            [VoiceModule.CMD[key] for key in keys],
            gap_ms=DIAGNOSIS_VOICE_GAP_MS,
            delay_ms=VOICE_START_DELAY_MS,
        )


def should_play_pest_voice(pest_name):
    now = now_ms()
    expired = []
    for name, last_at in recent_pest_voice.items():
        if now - last_at >= PEST_VOICE_COOLDOWN_MS:
            expired.append(name)
    for name in expired:
        recent_pest_voice.pop(name, None)

    last_at = recent_pest_voice.get(pest_name)
    if last_at is not None and now - last_at < PEST_VOICE_COOLDOWN_MS:
        return False

    recent_pest_voice[pest_name] = now
    return True


def queue_pest_diagnosis(pest_type, block=0):
    pest_name = (pest_type or '').strip()
    if pest_name == '褐斑病' and block == 6:
        keys = ['DETECT_HEBAN', 'AI_ANALYSIS', 'MANUAL_RECHECK']
    else:
        keys = PEST_DIAGNOSIS_SEQUENCES.get(pest_name)
    if not keys:
        keys = DIAGNOSIS_SEQUENCES.get(block, [])
    elif not should_play_pest_voice(pest_name):
        print('SKIP VOICE COOLDOWN', pest_name)
        return
    if keys:
        queue_voice_sequence(
            [VoiceModule.CMD[key] for key in keys],
            gap_ms=DIAGNOSIS_VOICE_GAP_MS,
            delay_ms=VOICE_START_DELAY_MS,
        )


def enqueue_event(block, event_type):
    event_id = '%s-%d-%d' % (event_type, block, now_ms())
    status = 'running' if event_type == 'nfc_detected' else 'completed'
    body = '{"event_id":"%s","robot_id":"%s","block_id":"A-%02d","event_type":"%s","status":"%s"}' % (
        event_id,
        DEVICE_ID,
        block,
        event_type,
        status,
    )
    pending_events.append(body)


def handle_frame(block):
    global last_block, last_completed_block
    frame = b'\xAA\xAA' + bytes([block]) + b'\xFF\xFF'
    stm32.write(frame)
    print('ECHO', frame)
    if TTL_SIM_VOICE_MODE and block in TTL_SIM_DIAGNOSIS_MAP:
        sim_block = TTL_SIM_DIAGNOSIS_MAP[block]
        print('SIM DIAG', sim_block)
        queue_block_diagnosis(sim_block)
        return
    if block == 0 and last_block:
        print('DONE')
        last_completed_block = last_block
        queue_voice(VoiceModule.CMD['TASK_DONE'])
        enqueue_event(last_block, 'spray_done')
    elif block == 0x07:
        print('TASK COMPLETE')
        queue_voice(VoiceModule.CMD['INACTIVATE'])
        enqueue_event(last_block, 'spray_done')
    elif 1 <= block <= 6:
        if last_block == block:
            print('BLOCK KEEP', block)
            enqueue_event(block, 'nfc_detected')
            return
        last_block = block
        print('BLOCK', block)
        queue_voice(
            VoiceModule.CMD['BLOCK%d' % block],
            gap_ms=BLOCK_VOICE_GAP_MS,
            delay_ms=VOICE_START_DELAY_MS,
        )
        if block in CROP_VOICE:
            queue_voice(CROP_VOICE[block])
        enqueue_event(block, 'nfc_detected')
    else:
        print('UNKNOWN BLOCK', block)


def handle_stm32():
    global buf
    if not stm32.any():
        return
    data = stm32.read(stm32.any())
    print('RX', data)
    buf.extend(data)
    while len(buf) >= 5:
        if buf[0] == 0xAA and buf[1] == 0xAA and buf[3] == 0xFF and buf[4] == 0xFF:
            handle_frame(buf[2])
            buf = buf[5:]
        else:
            buf = buf[1:]
    if len(buf) > 64:
        buf = bytearray()


def flush_events():
    if pending_events and wifi.isconnected():
        if post_event(pending_events[0]):
            print('EVENT OK')
            pending_events.pop(0)


last_req_id = ''

def handle_command():
    global last_req_id
    data = get_command()
    if not data:
        return
    try:
        text = data.decode()
        body = text[text.find('\r\n\r\n') + 4:]
        result = json.loads(body) if body else None
        if not result or not result.get('data'):
            return
        req_id = result.get('request_id', '')
        if req_id and req_id == last_req_id:
            return  # 已处理过，跳过重复下发
        last_req_id = req_id
        cmd = result['data']
        block = int(cmd.get('block', 0))
        pest_code = int(cmd.get('pest_code', 4))
        stm32.write(b'\xAA\xAA' + bytes([block, pest_code]) + b'\xFF\xFF')
        pest_type = cmd.get('pest_type', '')
        print('CMD block=', block, 'pest_code=', pest_code, 'pest=', pest_type)
        queue_pest_diagnosis(pest_type, block)
    except Exception as exc:
        print('CMD parse err:', exc)


print('hub start')
print('STM32 UART2 baud=', STM32_BAUD, 'tx=', STM32_TX, 'rx=', STM32_RX)
print('Voice UART1 baud=115200 tx=', VOICE_TX, 'rx=', VOICE_RX)
wifi.active(True)
wifi.connect(WIFI_SSID, WIFI_PASS)
print('WiFi connecting, TTL is active')
for _ in range(20):
    handle_stm32()
    if wifi.isconnected():
        break
    sleep_ms(500)
print('WiFi OK' if wifi.isconnected() else 'WiFi FAIL')
print('ready')

while True:
    handle_stm32()
    process_voice_queue()
    current = now_ms()
    if wifi.isconnected() and current - last_poll > POLL_MS:
        last_poll = current
        flush_events()
        handle_command()
    sleep_ms(20)