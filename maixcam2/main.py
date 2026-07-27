"""
MaixCam2 任务循环控制脚本。

一次任务流程:
1. ALIGN: 用绿色植株中心进行左右对准。
2. MOVE_MONITOR: 前进过程中持续监控绿色面积，并根据绿色中心继续左右微调。
3. DETECT: 只有绿色面积达到停止阈值后才停车，持续运行 YOLO 并上传。

串口包格式沿用旧的抓取工程:
AA AA x_dir y_dir x_dist_h x_dist_l y_dist_h y_dist_l flag FF FF

flag 含义:
0 = 运动指令
1 = 停止 / 到位 / 开始识别
2 = 目标丢失，停止
3 = 太近，后退或保护停机
"""

from maix import camera, display, image, nn, app, comm, time, pinmap, uart
import struct
import os
import requests


# =========================
# 基站上传参数
# =========================
BASE_URL = "http://<BASE_STATION_IP>:8000/api/device/images"  # 图片上传接口，部署时替换为实际基站 IP
FALLBACK_BLOCK_ID = ""        # 默认地块编号
SIM_DATE = ""                     # 模拟日期，留空表示用基站当前日期
UPLOAD_COOL = 5000                # 任意两次图片上传尝试至少间隔 5 秒
UPLOAD_TIMEOUT_S = 1              # 网络异常时最多阻塞 1 秒，避免画面长时间卡住
FRUIT_UPLOAD_COOL = 4000          # 作物图片上传的最小间隔(ms)
JPEG_QUALITY = 70                 # 上传 JPEG 质量
MIN_SHARPNESS = 60                # 图像太模糊时跳过上传


# =========================
# YOLO 病虫害识别参数
# =========================
DETECT_CONF_TH = 0.60             # 病虫害识别置信度阈值
FRUIT_CONF_TH = 0.60              # 果实识别置信度阈值
MIN_AREA_PCT = 0.03               # 小于该比例的 YOLO 框忽略
MAX_HOLD = 18                     # 最多跟踪 N 帧后上传当前类别的最佳图片
APPROACH_DISPLAY_EVERY_N = 1      # 靠近阶段每帧都刷新，左上角面积和状态变化更及时
DETECT_DISPLAY_EVERY_N = 2        # YOLO 阶段隔帧显示，减轻显示负载
AREA_SAMPLE_EVERY_N = 1           # 靠近阶段每帧都更新绿色面积，避免面积变化反应太慢
DETECT_GREEN_SAMPLE_EVERY_N = 2   # YOLO 阶段隔 N 帧检查一次绿色是否还存在
DETECT_EXIT_NO_GREEN_MS = 1200    # 连续这么久都看不到绿色，就认为当前树任务结束
DETECT_GREEN_MIN_PIXELS = 20      # YOLO阶段只过滤零星噪点；看到绿色就继续保持识别
POST_TASK_HOLD_MS = 7000          # 当前树结束后等待 STM32 完成 Y 回缩和 Z 回顶


# =========================
# 对准和前进运动参数
# =========================
ALIGN_WAIT_MS = 900               # 每次左右微调后等待时间(ms)，给位置模式留足执行时间
MOVE_FORWARD_DIR = 1             # 前进方向对应的 y_dir，方向反了就改这里
MOVE_BACK_DIR = 0                # 后退方向对应的 y_dir
X_RIGHT_DIR = 1                  # 目标在右边时使用的 x_dir
X_LEFT_DIR = 0                   # 目标在左边时使用的 x_dir
FORWARD_TRAVEL_DIST = 18        # 向 STM32 发送的前进距离上限；面积到阈值时会提前停车
FORWARD_MONITOR_MS = 2600         # 仅用于提示预计运动时间，不再用于触发 YOLO
ALIGN_MIN_STEPS = 4              # 左右微调的最小步数
ALIGN_MAX_STEPS = 16              # 左右微调的最大步数，防止单次横移过大
ALIGN_STEP_SCALE = 0.26           # 水平像素偏差换算为步数的比例，减小单次横向修正幅度
MOVE_ALIGN_INTERVAL_MS = 1200   # 前进中两次左右修正的最小间隔，避免上一条还没走完
MOVE_ALIGN_TOLERANCE = 60        # 前进中允许的水平偏差，放宽以减少震荡
MOVE_ALIGN_MIN_STEPS = 4         # 前进中左右微调的最小步数
MOVE_ALIGN_MAX_STEPS = 12        # 前进中左右微调的最大步数，收窄防止过冲
MOVE_ALIGN_STEP_SCALE = 0.05     # 前进中水平像素偏差换算为步数的比例，减半防止过冲


# =========================
# 靠近阶段的绿色目标参数
# 面积只用于运动中的提前停车保护，不再决定是否继续前进，也不使用宽度判断距离。
# 一排树会先按横向绿色带拆成多株候选，再选择最靠近画面中心的那一株作为当前目标。
# =========================
COLOR_W = 96                      # 低分辨率颜色扫描宽度
COLOR_H = 72                      # 低分辨率颜色扫描高度
MIN_COLOR_PIXELS = 35             # 绿色像素太少时视为噪声
MIN_PLANT_WIDTH = 5               # 一株候选最小横向宽度(低分辨率像素)
GREEN_COLUMN_MIN_PIXELS = 3       # 一列至少有这么多绿色像素，才算绿色列
GREEN_GAP_MERGE = 5             # 3 两株之间小缝隙小于这个值时合并
STOP_AREA = 0.32                # 绿色面积达到 32% 时停止靠近并开始 YOLO**
CANOPY_MERGE_GAP = 12             # 同一棵树冠左右碎块之间允许的横向合并间隔
CANOPY_MERGE_HEIGHT_DELTA = 0.18  # 两块高度占比接近时，允许合并为同一树冠
AREA_FILTER_SIZE = 3             # 最近 N 次面积取中值，减小单帧跳变
STOP_HIT_COUNT = 2               # 最近 N 次中至少有这么多次达到阈值才停止
X_ALIGN_TOLERANCE = 32         # 42目标中心距画面中心小于该像素时认为左右已对准
TARGET_LOST_LIMIT = 5             # 连续丢失目标 N 次后仅作状态参考，不再直接发送停止包
TARGET_MIN_AREA_PCT = 0.035     # 小于该面积占比的绿色块不当作当前树，避免背景小绿块误触发
TARGET_STABLE_MOVE_PX = 35      # 90连续两次中心偏差过大时重新累计稳定帧
TARGET_STABLE_AREA_DELTA = 0.12   # 连续两次面积变化过大时重新累计稳定帧
TARGET_LOCK_FRAMES = 3          # 绿色候选至少连续稳定这么多帧，才正式当作树目标
TARGET_SCORE_AREA_GAIN = 180      # 候选评分时面积权重，越大越偏向近处更大的树
TARGET_SCORE_CENTER_PENALTY = 1.4 # 候选评分时中心偏移惩罚，越大越偏向画面中间
TARGET_SCORE_TRACK_PENALTY = 3.2  # 2.2候选评分时对上一帧目标中心的偏移惩罚，减小左右跳株
ALIGN_TRIGGER_MARGIN = 10         # 静止对准时，只有明显超出容差才开始横向修正
ALIGN_CONFIRM_FRAMES = 3          # 静止对准时同方向至少连续确认这么多帧
MOVE_ALIGN_TRIGGER_MARGIN = 36    # 前进中只有明显偏出容差才补 X
MOVE_ALIGN_CONFIRM_FRAMES = 3     # 前进中补偿前，同方向至少连续确认这么多帧

GREEN_MIN = 53                    # 55绿色最小亮度
GREEN_DELTA = 16                  # 绿色至少比红/蓝高这么多
GREEN_DOMINANCE_NUM = 11          # 用于过滤灰色/黄色背景
GREEN_DOMINANCE_DEN = 10
FORWARD_START_GUARD_MS = 1200     # 发出Y轴前进启动包后，先给Y起步时间，期间不要让X抢占命令
FORWARD_RESEND_INTERVAL_MS = 180  # Y轴启动包的重发间隔
FORWARD_RESEND_MAX_COUNT = 3      # Y轴启动包最多连续重发次数，避免整个前进阶段一直刷包


# =========================
# 类别映射
# =========================
TARGET_CLASSES = {
    0: "heban",
    1: "baifen",
    2: "caiqingchong",
    3: "woniu",
}

PEST_LABEL_CN = {
    "heban": "褐斑病",
    "baifen": "白粉病",
    "caiqingchong": "菜青虫",
    "woniu": "蜗牛",
    "mihoutao": "猕猴桃",
    "juzi": "橘子",
}

FRUIT_BLOCK_MAP = {
    "mihoutao": "A-04",
    "juzi": "A-06",
}

FRUIT_LABEL_CN = {
    "mihoutao": "猕猴桃",
    "juzi": "橘子",
}

HIDDEN_DISPLAY_LABELS = {"jiankang", "healthy"}


# =========================
# 模型 / 摄像头 / 串口初始化
# =========================
model_path = "model_292463.mud"
if not os.path.exists(model_path):
    model_path = "/root/models/maixhub/292463/model_292463.mud"
detector = nn.YOLOv5(model=model_path)

CAM_W, CAM_H = 640, 480
MODEL_W, MODEL_H = detector.input_width(), detector.input_height()
IMAGE_CENTER_X = MODEL_W // 2
IMAGE_CENTER_Y = MODEL_H // 2

cam = camera.Camera(CAM_W, CAM_H)
dis = display.Display()

ser = None
try:
    pinmap.set_pin_function("A22", "UART4_RX")
    pinmap.set_pin_function("A21", "UART4_TX")
    ser = uart.UART("/dev/ttyS4", 115200)
    print("[UART] 串口 /dev/ttyS4 @115200 已就绪")
except Exception as e:
    print(f"[UART] 串口初始化失败: {e}")

APP_CMD_DETECT_RES = 0x02
report_on = False
p = comm.CommProtocol(buff_size=1024)
CAMERA_FLAG_READY = 5
CAMERA_FLAG_EXIT = 6
CAMERA_CMD_FORCE_DETECT = 8


# =========================
# 运行状态
# =========================
STATE_WAIT_READY = 0
STATE_ALIGN = 1
STATE_ALIGN_WAIT = 2
STATE_MOVE_MONITOR = 3
STATE_DETECT = 4

system_state = STATE_WAIT_READY
task_id = 1                         # 当前树木任务编号，从 1 开始
frame_idx = 0
event_seq = 0
last_upload_ms = 0
last_fruit_ms = 0
move_wait_until_ms = 0
forward_start_guard_until_ms = 0
last_forward_resend_ms = 0
forward_resend_count = 0
y_forward_started = False
last_move_align_ms = 0              # 上一次前进中发送左右微调指令的时间
last_x_motion_ms = 0                # 最近一次发送X速度指令的时间，用于限制过早停车
move_timeout_reported = False       # 是否已打印前进预计时间提示，防止重复输出
detect_start_ms = 0
lost_target_count = 0
loss_stop_sent = False
x_stop_sent = True                    # X 轴是否已发送停车包，避免无目标时重复刷包
detect_last_green_ms = 0
post_task_hold_until_ms = 0
post_detect_fast_start_until_ms = 0
align_pending_dir = -1
align_pending_count = 0
move_align_pending_dir = -1
move_align_pending_count = 0
upload_retry_after_ms = 0
area_history = []
uploaded_labels = set()             # 本轮已上传类别，发现新类别仍可继续上传
pending_uploads = {}                # 等待上传的各类别最佳检测结果

last_area_pct = -1.0
last_area_pixels = 0
last_area_bands = 0
last_width_pct = 0.0
last_height_pct = 0.0
last_decision_text = "WAIT"
last_target_box = None
last_filtered_area = -1.0
last_target_fill_ratio = 0.0        # 当前目标框内绿色填充率
last_target_bottom_pct = 0.0        # 当前目标底部所在画面高度占比
target_lock_count = 0               # 当前绿色候选已连续稳定的帧数
target_track_cx = -1                # 上一帧绿色候选中心 x
target_track_area = -1.0            # 上一帧绿色候选面积占比
uart_rx_state = 0
uart_rx_buf = bytearray()


def clamp(v, lo, hi):
    return max(lo, min(hi, int(v)))


def Data_Pack(x_dir, y_dir, x_dist, y_dist, flag=0):
    start = b"\xAA\xAA"
    end = b"\xFF\xFF"
    x_dir_byte = clamp(x_dir, 0, 255).to_bytes(1, "big")
    y_dir_byte = clamp(y_dir, 0, 255).to_bytes(1, "big")
    x_dist_bytes = clamp(x_dist, 0, 65535).to_bytes(2, "big")
    y_dist_bytes = clamp(y_dist, 0, 65535).to_bytes(2, "big")
    flag_byte = clamp(flag, 0, 255).to_bytes(1, "big")
    return start + x_dir_byte + y_dir_byte + x_dist_bytes + y_dist_bytes + flag_byte + end


def clear_uart_rx():
    if ser is None:
        return
    try:
        ser.read()
    except Exception:
        pass


def poll_uart_feedback():
    global uart_rx_state, uart_rx_buf, system_state
    global lost_target_count, loss_stop_sent, forward_resend_count, y_forward_started
    if ser is None:
        return
    try:
        data = ser.read()
    except Exception:
        return
    if not data:
        return
    if isinstance(data, int):
        data = bytes([data])

    for byte in data:
        if uart_rx_state == 0:
            uart_rx_state = 1 if byte == 0xAA else 0
        elif uart_rx_state == 1:
            if byte == 0xAA:
                uart_rx_state = 2
                uart_rx_buf = bytearray()
            else:
                uart_rx_state = 0
        else:
            uart_rx_buf.append(byte)
            if len(uart_rx_buf) >= 3:
                short_payload = bytes(uart_rx_buf[:3])
                if short_payload[1] == 0xFF and short_payload[2] == 0xFF:
                    uart_rx_state = 0
                    uart_rx_buf = bytearray()
                    cmd = short_payload[0]
                    if cmd == CAMERA_CMD_FORCE_DETECT:
                        if system_state in (STATE_ALIGN, STATE_ALIGN_WAIT, STATE_MOVE_MONITOR):
                            print("[UART] 收到 STM32 FORCE DETECT，直接进入 YOLO")
                            start_detect_window()
                        else:
                            print("[UART] 收到 STM32 FORCE DETECT，但当前不在靠近流程，忽略")
                    continue

            if len(uart_rx_buf) < 9:
                continue

            payload = bytes(uart_rx_buf[:9])
            uart_rx_state = 0
            uart_rx_buf = bytearray()
            if payload[7] != 0xFF or payload[8] != 0xFF:
                continue
            flag = payload[6]
            if flag == CAMERA_CMD_FORCE_DETECT:
                if system_state in (STATE_ALIGN, STATE_ALIGN_WAIT, STATE_MOVE_MONITOR):
                    print("[UART] 收到 STM32 FORCE DETECT(long)，直接进入 YOLO")
                    start_detect_window()
                else:
                    print("[UART] 收到 STM32 FORCE DETECT(long)，但当前不在靠近流程，忽略")
            elif flag == CAMERA_FLAG_READY and system_state == STATE_WAIT_READY:
                reset_task_visual_cache()
                area_history.clear()
                system_state = STATE_ALIGN
                print("[UART] 收到 STM32 READY，开始视觉引导")
            elif flag == CAMERA_FLAG_EXIT:
                stop_x_once()
                reset_task_visual_cache()
                area_history.clear()
                lost_target_count = 0
                loss_stop_sent = False
                forward_resend_count = 0
                y_forward_started = False
                system_state = STATE_WAIT_READY
                print("[UART] 收到 STM32 EXIT，退出视觉模式，等待下一次 READY")


def send_motion_packet(x_dir, y_dir, x_dist, y_dist, flag):
    global loss_stop_sent, x_stop_sent, last_x_motion_ms
    if flag == 2:
        if loss_stop_sent:
            return
        loss_stop_sent = True
    if flag == 0 and x_dist > 0:
        x_stop_sent = False
        last_x_motion_ms = time.ticks_ms()
    elif flag != 0 or x_dist == 0:
        x_stop_sent = True
    if ser is not None:
        try:
            ser.write(Data_Pack(x_dir, y_dir, x_dist, y_dist, flag))
        except Exception as e:
            print(f"[UART] 发包失败: {e}")
    print(f"[UART] flag={flag} x_dir={x_dir} y_dir={y_dir} x_dist={x_dist} y_dist={y_dist}")


def stop_x_once():
    """确认居中或连续丢失目标后，只发一次 X 速度停止包。"""
    if x_stop_sent:
        return
    send_motion_packet(0, 0, 0, 0, 0)


def resend_forward_command(now_ms):
    global last_forward_resend_ms, forward_resend_count
    if forward_resend_count >= FORWARD_RESEND_MAX_COUNT:
        return False
    if now_ms - last_forward_resend_ms < FORWARD_RESEND_INTERVAL_MS:
        return False
    send_motion_packet(0, MOVE_FORWARD_DIR, 0, FORWARD_TRAVEL_DIST, 0)
    last_forward_resend_ms = now_ms
    forward_resend_count += 1
    return True


def force_forward_start(now_ms):
    global last_forward_resend_ms, forward_resend_count, y_forward_started
    send_motion_packet(0, MOVE_FORWARD_DIR, 0, FORWARD_TRAVEL_DIST, 0)
    last_forward_resend_ms = now_ms
    forward_resend_count = 1
    y_forward_started = True


MOVE_ALIGN_RESTART_MARGIN = 24

def gen_event_id():
    global event_seq
    event_seq += 1
    return f"CAM-{time.ticks_ms()}-{event_seq:04d}"


def encode_objs(objs):
    body = b""
    for obj in objs:
        body += struct.pack("<hhHHHf", obj.x, obj.y, obj.w, obj.h, obj.class_id, obj.score)
    return body


def calc_sharpness(img):
    small = img.resize(96, 72)
    buf = small.to_bytes()
    total = 0
    count = 0
    prev_bright = None
    for i in range(0, len(buf) - 3, 3):
        bright = (buf[i] + buf[i + 1] + buf[i + 2]) // 3
        if prev_bright is not None:
            diff = bright - prev_bright
            total += diff * diff
            count += 1
        prev_bright = bright
    return total / count if count > 0 else 0


def resolve_upload_block(label):
    return FRUIT_BLOCK_MAP.get(label, FALLBACK_BLOCK_ID)


def build_frame_overlays(objs):
    overlays = []
    for obj in objs:
        label = detector.labels[obj.class_id].strip().lower()
        if label not in HIDDEN_DISPLAY_LABELS:
            overlays.append((label, obj.score, (obj.x, obj.y, obj.w, obj.h)))
    return overlays


def build_fruit_overlays(objs):
    overlays = []
    for obj in objs:
        label = detector.labels[obj.class_id].strip().lower()
        if label in FRUIT_BLOCK_MAP and obj.score >= FRUIT_CONF_TH:
            overlays.append((label, obj.score, (obj.x, obj.y, obj.w, obj.h)))
    return overlays


def cache_sample_info(target, decision):
    global last_area_pct, last_area_pixels, last_area_bands
    global last_width_pct, last_height_pct, last_decision_text, last_target_box
    global last_target_fill_ratio, last_target_bottom_pct

    last_decision_text = decision["message"]
    if target is None:
        last_area_pct = -1.0
        last_area_pixels = 0
        last_area_bands = 0
        last_width_pct = 0.0
        last_height_pct = 0.0
        last_target_fill_ratio = 0.0
        last_target_bottom_pct = 0.0
        last_target_box = None
        return

    last_area_pct = target["area_pct"]
    last_area_pixels = target["area"]
    last_area_bands = target.get("bands", 1)
    last_width_pct = target.get("width_pct", 0.0)
    last_height_pct = target.get("height_pct", 0.0)
    last_target_fill_ratio = target.get("fill_ratio", 0.0)
    last_target_bottom_pct = target.get("bottom_pct", 0.0)
    last_target_box = target["box"]


def cache_live_target_info(target):
    global last_area_pct, last_area_pixels, last_area_bands
    global last_width_pct, last_height_pct, last_target_box

    if target is None:
        last_area_pct = -1.0
        last_area_pixels = 0
        last_area_bands = 0
        last_width_pct = 0.0
        last_height_pct = 0.0
        last_target_box = None
        return

    last_area_pct = target["area_pct"]
    last_area_pixels = target["area"]
    last_area_bands = target.get("bands", 1)
    last_width_pct = target.get("width_pct", 0.0)
    last_height_pct = target.get("height_pct", 0.0)
    last_target_box = target["box"]


def draw_status_overlay(img_yolo, title_text):
    img_yolo.draw_string(5, 5, title_text, color=image.COLOR_GREEN, scale=1.2)
    if last_area_pct >= 0:
        img_yolo.draw_string(
            5, 28,
            f"AREA {last_area_pct:.4f} {last_area_pct * 100:.2f}%",
            color=image.COLOR_GREEN,
            scale=1.1
        )
        img_yolo.draw_string(
            5, 50,
            f"MED {last_filtered_area:.4f} XERR {target_center_error()}",
            color=image.COLOR_GREEN,
            scale=1.0
        )
        img_yolo.draw_string(
            5, 72,
            f"W {last_width_pct:.2f} H {last_height_pct:.2f}",
            color=image.COLOR_GREEN,
            scale=1.0
        )
        img_yolo.draw_string(
            5, 94,
            f"FILL {last_target_fill_ratio:.2f} BOT {last_target_bottom_pct:.2f}",
            color=image.COLOR_GREEN,
            scale=1.0
        )
    else:
        img_yolo.draw_string(5, 28, "AREA --", color=image.COLOR_GREEN, scale=1.1)

    if last_target_box is not None and system_state in (STATE_ALIGN, STATE_ALIGN_WAIT, STATE_MOVE_MONITOR):
        x, y, w, h = last_target_box
        img_yolo.draw_rect(x, y, w, h, color=image.COLOR_GREEN, thickness=2)


def draw_detect_overlay(img_yolo, title_text):
    """YOLO 阶段单独显示状态，避免和靠近阶段的绿色框/面积信息混在一起。"""
    img_yolo.draw_string(5, 5, title_text, color=image.COLOR_RED, scale=1.2)
    img_yolo.draw_string(5, 28, f"TASK {task_id} DETECT", color=image.COLOR_RED, scale=1.0)


def target_center_error(target=None):
    """返回绿色目标中心相对画面中心的水平偏差，正数表示目标在右边。"""
    box = target["box"] if target is not None else last_target_box
    if box is None:
        return 0
    x, _, w, _ = box
    return x + w // 2 - IMAGE_CENTER_X


def update_area_filter(area_pct):
    """保存少量面积样本并返回中值，抑制叶片边缘造成的单帧面积突变。"""
    global last_filtered_area
    area_history.append(area_pct)
    while len(area_history) > AREA_FILTER_SIZE:
        area_history.pop(0)
    ordered = sorted(area_history)
    last_filtered_area = ordered[len(ordered) // 2]
    return last_filtered_area


def area_reached_stop():
    """当前面积中值到位，且最近样本中至少两次超过阈值，才确认停止。"""
    hits = 0
    for value in area_history:
        if value >= STOP_AREA:
            hits += 1
    return last_filtered_area >= STOP_AREA and hits >= STOP_HIT_COUNT


def upload_photo(img_full, label, score, box_448, overlays=None):
    global last_upload_ms, upload_retry_after_ms

    now_ms = time.ticks_ms()
    if now_ms < upload_retry_after_ms:
        return False

    # 从本次尝试开始计时，不论成功或失败，下一次上传都至少等待 5 秒。
    last_upload_ms = now_ms
    upload_retry_after_ms = now_ms + UPLOAD_COOL

    sharp = calc_sharpness(img_full)
    print(f"[SHARP] score={sharp:.0f}")
    if sharp < MIN_SHARPNESS:
        print(f"[SHARP] 图像偏模糊，跳过上传，threshold={MIN_SHARPNESS}")
        last_upload_ms = time.ticks_ms()
        return False

    sx = CAM_W / MODEL_W
    sy = CAM_H / MODEL_H
    x, y, w, h = box_448
    fx = int(x * sx)
    fy = int(y * sy)
    fw = int(w * sx)
    fh = int(h * sy)

    draw_items = overlays or [(label, score, box_448)]
    for draw_label, draw_score, draw_box in draw_items:
        dx, dy, dw, dh = draw_box
        dfx = int(dx * sx)
        dfy = int(dy * sy)
        dfw = int(dw * sx)
        dfh = int(dh * sy)
        img_full.draw_rect(dfx, dfy, dfw, dfh, color=image.COLOR_RED, thickness=4)
        img_full.draw_string(
            dfx, dfy - 24, f"{draw_label}:{draw_score:.2f}",
            color=image.COLOR_RED, scale=1.8
        )

    try:
        jpg = img_full.to_jpeg(quality=JPEG_QUALITY)
        files = {"file": ("maixcam_photo.jpg", jpg.to_bytes(), "image/jpeg")}
        data = {
            "event_id": gen_event_id(),
            "pest_type": label,
            "block_id": resolve_upload_block(label),
        }
        if SIM_DATE:
            data["sim_date"] = SIM_DATE

        print(f"[UPLOAD] pest={label} conf={score:.2f} block={data['block_id']} box=({fx},{fy},{fw},{fh})")
        resp = requests.post(BASE_URL, data=data, files=files, timeout=UPLOAD_TIMEOUT_S)
        if resp.status_code == 200:
            try:
                body = resp.json()
            except Exception:
                print("[UPLOAD] 上传失败：基站返回 200 但响应不是 JSON")
                return False

            accepted = body.get("accepted", True)
            ignored = body.get("ignored", False)
            if not accepted or ignored:
                print(
                    f"[UPLOAD] 基站忽略 event_id={data['event_id']} "
                    f"reason={body.get('reason', 'unknown')} msg={body.get('message', '-')}"
                )
                return False

            print(
                f"[UPLOAD] 上传成功 url={body.get('image_url', '?')} "
                f"pest_record={body.get('pest_record_status', '-')}"
            )
            return True
        print(f"[UPLOAD] 上传失败 {resp.status_code} {resp.text}")
        return False
    except Exception as e:
        print(f"[UPLOAD] 上传异常 {e}")
        return False


def detect_has_valid_green_target(img_full):
    """进入 YOLO 后只判断绿色是否仍存在，不再套用靠近阶段的面积和稳定锁定。"""
    small = img_full.resize(COLOR_W, COLOR_H)
    buf = small.to_bytes()
    green_pixels = 0
    for i in range(0, len(buf) - 2, 3):
        if is_green_pixel(buf[i], buf[i + 1], buf[i + 2]):
            green_pixels += 1
            if green_pixels >= DETECT_GREEN_MIN_PIXELS:
                return True
    return False


def is_green_pixel(r, g, b):
    return (
        g > GREEN_MIN
        and g - r > GREEN_DELTA
        and g - b > GREEN_DELTA
        and g * GREEN_DOMINANCE_DEN > r * GREEN_DOMINANCE_NUM
        and g * GREEN_DOMINANCE_DEN > b * GREEN_DOMINANCE_NUM
    )


def green_plant_target(img_full):
    global target_lock_count, target_track_cx, target_track_area

    small = img_full.resize(COLOR_W, COLOR_H)
    buf = small.to_bytes()
    col_counts = [0] * COLOR_W
    green_points = []

    # 第一次扫描：找出绿色像素，并统计每一列有多少绿色像素。
    for y in range(COLOR_H):
        row = y * COLOR_W * 3
        for x in range(COLOR_W):
            i = row + x * 3
            r = buf[i]
            g = buf[i + 1]
            b = buf[i + 2]
            if is_green_pixel(r, g, b):
                col_counts[x] += 1
                green_points.append((x, y))

    if len(green_points) < MIN_COLOR_PIXELS:
        target_lock_count = 0
        target_track_cx = -1
        target_track_area = -1.0
        return None

    # 按横向绿色列拆成多株候选，避免把整排树一次性当成一个大目标。
    bands = []
    start = None
    last_green = -1
    for x, count in enumerate(col_counts):
        is_col_green = count >= GREEN_COLUMN_MIN_PIXELS
        if is_col_green:
            if start is None:
                start = x
            last_green = x
        elif start is not None and x - last_green > GREEN_GAP_MERGE:
            if last_green - start + 1 >= MIN_PLANT_WIDTH:
                bands.append((start, last_green))
            start = None
            last_green = -1
    if start is not None and last_green - start + 1 >= MIN_PLANT_WIDTH:
        bands.append((start, last_green))

    if not bands:
        target_lock_count = 0
        target_track_cx = -1
        target_track_area = -1.0
        return None

    center_x = COLOR_W // 2
    candidates = []
    for x0, x1 in bands:
        min_y = COLOR_H
        max_y = -1
        pixels = 0
        for x, y in green_points:
            if x0 <= x <= x1:
                pixels += 1
                if y < min_y:
                    min_y = y
                if y > max_y:
                    max_y = y
        if pixels < MIN_COLOR_PIXELS or max_y <= min_y:
            continue
        band_center = (x0 + x1) // 2
        band_w = x1 - x0 + 1
        band_h = max_y - min_y + 1
        area_pct = pixels / (COLOR_W * COLOR_H)
        width_pct = band_w / COLOR_W
        height_pct = band_h / COLOR_H
        fill_ratio = pixels / (band_w * band_h)
        bottom_pct = (max_y + 1) / COLOR_H

        if area_pct < TARGET_MIN_AREA_PCT:
            continue

        score = (
            area_pct * TARGET_SCORE_AREA_GAIN
            - abs(band_center - center_x) * TARGET_SCORE_CENTER_PENALTY
        )
        if target_track_cx >= 0:
            score -= abs(band_center - target_track_cx) * TARGET_SCORE_TRACK_PENALTY
        candidates.append({
            "x0": x0,
            "x1": x1,
            "min_y": min_y,
            "max_y": max_y,
            "pixels": pixels,
            "band_center": band_center,
            "area_pct": area_pct,
            "width_pct": width_pct,
            "height_pct": height_pct,
            "fill_ratio": fill_ratio,
            "bottom_pct": bottom_pct,
            "score": score,
        })

    if not candidates:
        target_lock_count = 0
        target_track_cx = -1
        target_track_area = -1.0
        return None

    candidates.sort(key=lambda c: c["x0"])
    merged_candidates = []
    for candidate in candidates:
        if not merged_candidates:
            merged_candidates.append(candidate)
            continue

        prev = merged_candidates[-1]
        gap = candidate["x0"] - prev["x1"] - 1
        height_delta = abs(candidate["height_pct"] - prev["height_pct"])
        overlaps_y = not (
            candidate["min_y"] > prev["max_y"] or candidate["max_y"] < prev["min_y"]
        )

        if gap <= CANOPY_MERGE_GAP and (overlaps_y or height_delta <= CANOPY_MERGE_HEIGHT_DELTA):
            x0 = min(prev["x0"], candidate["x0"])
            x1 = max(prev["x1"], candidate["x1"])
            min_y = min(prev["min_y"], candidate["min_y"])
            max_y = max(prev["max_y"], candidate["max_y"])
            pixels = prev["pixels"] + candidate["pixels"]
            band_w = x1 - x0 + 1
            band_h = max_y - min_y + 1
            area_pct = pixels / (COLOR_W * COLOR_H)
            width_pct = band_w / COLOR_W
            height_pct = band_h / COLOR_H
            fill_ratio = pixels / (band_w * band_h)
            band_center = (x0 + x1) // 2
            bottom_pct = (max_y + 1) / COLOR_H
            score = (
                area_pct * TARGET_SCORE_AREA_GAIN
                - abs(band_center - center_x) * TARGET_SCORE_CENTER_PENALTY
            )
            if target_track_cx >= 0:
                score -= abs(band_center - target_track_cx) * TARGET_SCORE_TRACK_PENALTY
            merged_candidates[-1] = {
                "x0": x0,
                "x1": x1,
                "min_y": min_y,
                "max_y": max_y,
                "pixels": pixels,
                "band_center": band_center,
                "area_pct": area_pct,
                "width_pct": width_pct,
                "height_pct": height_pct,
                "fill_ratio": fill_ratio,
                "bottom_pct": bottom_pct,
                "score": score,
            }
        else:
            merged_candidates.append(candidate)

    candidates = merged_candidates

    best = None
    for candidate in candidates:
        if best is None or candidate["score"] > best["score"]:
            best = candidate

    center_shift = abs(best["band_center"] - target_track_cx) if target_track_cx >= 0 else 0
    area_shift = abs(best["area_pct"] - target_track_area) if target_track_area >= 0 else 0.0
    if target_track_cx < 0:
        target_lock_count = 1
    elif center_shift <= TARGET_STABLE_MOVE_PX and area_shift <= TARGET_STABLE_AREA_DELTA:
        target_lock_count += 1
    else:
        target_lock_count = 1

    target_track_cx = best["band_center"]
    target_track_area = best["area_pct"]
    if target_lock_count < TARGET_LOCK_FRAMES:
        return None

    sx = MODEL_W / COLOR_W
    sy = MODEL_H / COLOR_H
    band_w = best["x1"] - best["x0"] + 1
    band_h = best["max_y"] - best["min_y"] + 1
    box = (
        int(best["x0"] * sx),
        int(best["min_y"] * sy),
        int(band_w * sx),
        int(band_h * sy),
    )
    area_pct = best["pixels"] / (COLOR_W * COLOR_H)
    return {
        "source": "green",
        "box": box,
        "area": best["pixels"],
        "area_pct": best["area_pct"],
        "bands": len(bands),
        "width_pct": best["width_pct"],
        "height_pct": best["height_pct"],
        "fill_ratio": best["fill_ratio"],
        "bottom_pct": best["bottom_pct"],
        "lock_count": target_lock_count,
    }


def find_marker_target(img_full):
    return green_plant_target(img_full)


def decide_approach_move(target):
    """先根据绿色中心左右对准，再发送前进命令并进入视觉监控。"""
    global align_pending_dir, align_pending_count
    if target is None:
        align_pending_dir = -1
        align_pending_count = 0
        return {
            "action": "target_lost",
            "flag": 2,
            "x_dir": 0,
            "y_dir": 0,
            "x_dist": 0,
            "y_dist": 0,
            "wait_ms": 0,
            "next_state": STATE_ALIGN,
            "message": "暂时没有找到绿色植株",
        }

    area = target["area_pct"]
    filtered_area = update_area_filter(area)
    dx = target_center_error(target)

    if abs(dx) > (X_ALIGN_TOLERANCE + ALIGN_TRIGGER_MARGIN):
        current_dir = 1 if dx > 0 else 0
        if align_pending_dir != current_dir:
            align_pending_dir = current_dir
            align_pending_count = 1
            return {
                "action": "align_confirm",
                "send_packet": False,
                "flag": 0,
                "x_dir": 0,
                "y_dir": 0,
                "x_dist": 0,
                "y_dist": 0,
                "wait_ms": 120,
                "next_state": STATE_ALIGN,
                "message": f"方向确认 dx={dx}",
            }
        align_pending_count += 1
        if align_pending_count < ALIGN_CONFIRM_FRAMES:
            return {
                "action": "align_confirm",
                "send_packet": False,
                "flag": 0,
                "x_dir": 0,
                "y_dir": 0,
                "x_dist": 0,
                "y_dist": 0,
                "wait_ms": 120,
                "next_state": STATE_ALIGN,
                "message": f"方向确认 dx={dx}",
            }
        x_steps = clamp(abs(dx) * ALIGN_STEP_SCALE, ALIGN_MIN_STEPS, ALIGN_MAX_STEPS)
        x_dir = X_RIGHT_DIR if dx > 0 else X_LEFT_DIR
        align_pending_count = 0
        return {
            "action": "align_right" if dx > 0 else "align_left",
            "send_packet": True,
            "flag": 0,
            "x_dir": x_dir,
            "y_dir": 0,
            "x_dist": x_steps,
            "y_dist": 0,
            "wait_ms": ALIGN_WAIT_MS,
            "next_state": STATE_ALIGN_WAIT,
            "message": f"目标{'右' if dx > 0 else '左'}偏 dx={dx}，横向微调 {x_steps} 步",
        }

    if area_reached_stop():
        align_pending_dir = -1
        align_pending_count = 0
        return {
            "action": "area_stop",
            "send_packet": True,
            "flag": 1,
            "x_dir": 0,
            "y_dir": 0,
            "x_dist": 0,
            "y_dist": 0,
            "wait_ms": 0,
            "next_state": STATE_DETECT,
            "message": f"面积到位 raw={area:.4f} med={filtered_area:.4f}，停止并开始识别",
        }

    align_pending_dir = -1
    align_pending_count = 0
    return {
        "action": "MOVING",
        "send_packet": True,
        "flag": 0,
        "x_dir": 0,
        "y_dir": MOVE_FORWARD_DIR,
        "x_dist": 0,
        "y_dist": FORWARD_TRAVEL_DIST,
        "wait_ms": FORWARD_MONITOR_MS,
        "next_state": STATE_MOVE_MONITOR,
        "message": f"已对准 dx={dx}，开始前进并持续监控，area={area:.4f}",
    }


def correct_horizontal_while_moving(target, now_ms):
    """前进时绿色整体偏向一侧，就限频发送一次较小的左右修正。"""
    global last_move_align_ms, forward_start_guard_until_ms
    global move_align_pending_dir, move_align_pending_count

    if target is None or now_ms - last_move_align_ms < MOVE_ALIGN_INTERVAL_MS:
        return False

    if now_ms < forward_start_guard_until_ms:
        return False

    dx = target_center_error(target)
    if abs(dx) <= (MOVE_ALIGN_TOLERANCE + MOVE_ALIGN_RESTART_MARGIN):
        move_align_pending_dir = -1
        move_align_pending_count = 0
        return False

    if abs(dx) <= (MOVE_ALIGN_TOLERANCE + MOVE_ALIGN_TRIGGER_MARGIN):
        move_align_pending_dir = -1
        move_align_pending_count = 0
        return False

    current_dir = 1 if dx > 0 else 0
    if move_align_pending_dir != current_dir:
        move_align_pending_dir = current_dir
        move_align_pending_count = 1
        return False

    move_align_pending_count += 1
    if move_align_pending_count < MOVE_ALIGN_CONFIRM_FRAMES:
        return False

    x_steps = clamp(
        abs(dx) * MOVE_ALIGN_STEP_SCALE,
        MOVE_ALIGN_MIN_STEPS,
        MOVE_ALIGN_MAX_STEPS,
    )
    x_dir = X_RIGHT_DIR if dx > 0 else X_LEFT_DIR
    send_motion_packet(x_dir, 0, x_steps, 0, 0)
    last_move_align_ms = now_ms
    move_align_pending_dir = -1
    move_align_pending_count = 0
    print(
        f"[MOVE] 绿色整体{'右' if dx > 0 else '左'}偏 dx={dx}，"
        f"前进中横向微调 {x_steps} 步"
    )
    return True


def reset_task_visual_cache():
    global last_target_box, last_area_pct, last_area_pixels, last_area_bands
    global last_width_pct, last_height_pct, last_filtered_area, last_decision_text
    global last_target_fill_ratio, last_target_bottom_pct
    global target_lock_count, target_track_cx, target_track_area
    global align_pending_dir, align_pending_count
    global move_align_pending_dir, move_align_pending_count
    global y_forward_started
    area_history.clear()
    last_target_box = None
    last_area_pct = -1.0
    last_area_pixels = 0
    last_area_bands = 0
    last_width_pct = 0.0
    last_height_pct = 0.0
    last_target_fill_ratio = 0.0
    last_target_bottom_pct = 0.0
    last_filtered_area = -1.0
    target_lock_count = 0
    target_track_cx = -1
    target_track_area = -1.0
    align_pending_dir = -1
    align_pending_count = 0
    move_align_pending_dir = -1
    move_align_pending_count = 0
    y_forward_started = False
    last_decision_text = "WAIT"


def start_detect_window():
    global system_state, uploaded_labels, pending_uploads, detect_last_green_ms
    global last_decision_text, y_forward_started
    uploaded_labels = set()
    pending_uploads = {}
    reset_task_visual_cache()
    last_decision_text = "YOLO"
    y_forward_started = False
    detect_last_green_ms = time.ticks_ms()
    send_motion_packet(0, 0, 0, 0, 1)
    system_state = STATE_DETECT
    print("[TASK] 已停车，进入持续 YOLO 识别；不会自动返回距离判断")


def finish_current_tree(reason):
    global system_state, task_id, lost_target_count, detect_last_green_ms
    global uploaded_labels, pending_uploads, loss_stop_sent, post_task_hold_until_ms
    global last_forward_resend_ms, forward_resend_count
    global post_detect_fast_start_until_ms
    global y_forward_started

    send_motion_packet(0, 0, 0, 0, 4)
    uploaded_labels = set()
    pending_uploads = {}
    lost_target_count = 0
    loss_stop_sent = False
    detect_last_green_ms = 0
    last_forward_resend_ms = 0
    forward_resend_count = 0
    y_forward_started = False
    post_task_hold_until_ms = time.ticks_ms() + POST_TASK_HOLD_MS
    post_detect_fast_start_until_ms = time.ticks_ms() + 1800
    reset_task_visual_cache()
    system_state = STATE_WAIT_READY
    print(f"[TASK {task_id}] 当前树任务结束: {reason}")
    task_id += 1
    print(f"[TASK {task_id}] 等待 STM32 READY")


def run_detect_cycle(img_full, img_yolo, should_display):
    global last_fruit_ms, pending_uploads, uploaded_labels

    objs = detector.detect(img_yolo, conf_th=DETECT_CONF_TH, iou_th=0.45)
    if len(objs) > 0 and report_on:
        p.report(APP_CMD_DETECT_RES, encode_objs(objs))

    frame_area = MODEL_W * MODEL_H
    frame_overlays = build_frame_overlays(objs)

    for obj in objs:
        label = detector.labels[obj.class_id].strip().lower()
        if should_display and label not in HIDDEN_DISPLAY_LABELS:
            img_yolo.draw_rect(obj.x, obj.y, obj.w, obj.h, color=image.COLOR_RED, thickness=2)
            img_yolo.draw_string(
                obj.x, obj.y - 18, f"{label}:{obj.score:.2f}",
                color=image.COLOR_RED, scale=1.0
            )

        is_pest = label in TARGET_CLASSES.values() and obj.score >= DETECT_CONF_TH
        is_fruit = label in FRUIT_BLOCK_MAP and obj.score >= FRUIT_CONF_TH
        if not is_pest and not is_fruit:
            continue

        area = obj.w * obj.h
        if area / frame_area < MIN_AREA_PCT:
            continue

        candidate = pending_uploads.get(label)
        if candidate is None:
            candidate = {
                "img": img_full,
                "score": obj.score,
                "area": area,
                "box": (obj.x, obj.y, obj.w, obj.h),
                "overlays": frame_overlays,
                "seen": 1,
                "last_seen": frame_idx,
            }
            pending_uploads[label] = candidate
        else:
            candidate["seen"] += 1
            candidate["last_seen"] = frame_idx
            # 面积更大或置信度明显更高时，替换成该类别当前最佳图片。
            if area > candidate["area"] or obj.score > candidate["score"] + 0.08:
                candidate["img"] = img_full
                candidate["score"] = obj.score
                candidate["area"] = area
                candidate["box"] = (obj.x, obj.y, obj.w, obj.h)
                candidate["overlays"] = frame_overlays

    now_ms = time.ticks_ms()
    cooling = (now_ms - last_upload_ms) < UPLOAD_COOL

    if not cooling:
        for label, candidate in pending_uploads.items():
            target_disappeared = frame_idx - candidate["last_seen"] >= 3
            held_long_enough = candidate["seen"] >= MAX_HOLD
            if label in uploaded_labels:
                continue
            if not (target_disappeared or held_long_enough):
                print(
                    f"[DETECT] 候选未成熟 label={label} seen={candidate['seen']} "
                    f"last_gap={frame_idx - candidate['last_seen']}"
                )
                continue
            print(
                f"[DETECT] 上传新类别 {label} score={candidate['score']:.2f} "
                f"area={candidate['area'] / frame_area:.1%}"
            )
            uploaded = upload_photo(
                candidate["img"], label, candidate["score"],
                candidate["box"], candidate["overlays"]
            )
            if uploaded:
                uploaded_labels.add(label)
                if label in FRUIT_BLOCK_MAP:
                    last_fruit_ms = now_ms
            else:
                print(f"[DETECT] 上传未通过 label={label}，等待下一轮重新成熟后再试")
            break

    if cooling:
        rem = (UPLOAD_COOL - (now_ms - last_upload_ms)) // 1000
        print(f"[DETECT] 上传冷却中 remaining={rem}s pending={len(pending_uploads)}")
        return f"YOLO COOL {rem}s"
    if uploaded_labels:
        return f"YOLO SENT {len(uploaded_labels)}"
    return f"YOLO SCAN {len(pending_uploads)}"


# print("=" * 48)
# print(f"模型: {model_path}")
# print(f"摄像头: {CAM_W}x{CAM_H}  YOLO输入: {MODEL_W}x{MODEL_H}")
# print(f"基站: {BASE_URL}  默认地块: {FALLBACK_BLOCK_ID}")
# print(f"任务流程: 左右对准 -> 前进中继续横向微调 -> AREA到{STOP_AREA:.2f}后YOLO识别")
# print(f"绿色目标: 按横向绿色带分株，选择最靠近画面中心的一株")
# print(f"只有AREA>={STOP_AREA:.2f}才进入YOLO；前进计时结束不会进入识别")
# print(f"YOLO中无绿色超过{DETECT_EXIT_NO_GREEN_MS}ms则结束当前树")
# print(f"X方向映射: LEFT->{X_LEFT_DIR} RIGHT->{X_RIGHT_DIR}")
# print("=" * 48)


while not app.need_exit():
    frame_idx += 1
    now_ms = time.ticks_ms()
    poll_uart_feedback()

    img_full = cam.read()
    img_yolo = img_full.resize(MODEL_W, MODEL_H)
    live_target = None
    if system_state in (STATE_WAIT_READY, STATE_ALIGN, STATE_ALIGN_WAIT, STATE_MOVE_MONITOR):
        should_display = (frame_idx % APPROACH_DISPLAY_EVERY_N) == 0
    else:
        should_display = (frame_idx % DETECT_DISPLAY_EVERY_N) == 0

    # 绿色扫描只服务于靠近过程。进入 YOLO 后不再画绿色框，避免拖慢识别。
    if system_state in (STATE_ALIGN, STATE_MOVE_MONITOR) and (frame_idx % AREA_SAMPLE_EVERY_N) == 0:
        live_target = find_marker_target(img_full)
        cache_live_target_info(live_target)

    if system_state == STATE_WAIT_READY:
        stop_x_once()
        lost_target_count = 0
        if should_display:
            draw_status_overlay(img_yolo, "WAIT STM32")
            dis.show(img_yolo)
        continue

    if system_state == STATE_ALIGN:
        # flag=4 后必须先让 STM32 完成当前树回位，不能抢发下一棵的 X/Y 包。
        if now_ms < post_task_hold_until_ms:
            stop_x_once()
            lost_target_count = 0
            if should_display:
                draw_status_overlay(img_yolo, "WAIT RETURN")
                dis.show(img_yolo)
            continue

        target = live_target
        if target is None and (frame_idx % AREA_SAMPLE_EVERY_N) != 0:
            if should_display:
                draw_status_overlay(img_yolo, "WAIT TARGET")
                dis.show(img_yolo)
            continue

        decision = decide_approach_move(target)
        cache_sample_info(target, decision)

        if target is None:
            if now_ms < post_task_hold_until_ms:
                stop_x_once()
                lost_target_count = 0
                if should_display:
                    draw_status_overlay(img_yolo, "WAIT RETURN")
                    dis.show(img_yolo)
                continue

            lost_target_count += 1
            area_history.clear()
            last_filtered_area = -1.0
            if lost_target_count == TARGET_LOST_LIMIT:
                stop_x_once()
                print("[APPROACH] 当前画面无树，继续等待下一棵进入视野")
        else:
            lost_target_count = 0
            loss_stop_sent = False
            if (
                now_ms >= post_task_hold_until_ms
                and now_ms < post_detect_fast_start_until_ms
                and abs(target_center_error(target)) <= MOVE_ALIGN_TOLERANCE
            ):
                stop_x_once()
                force_forward_start(now_ms)
                forward_start_guard_until_ms = now_ms + FORWARD_START_GUARD_MS
                move_wait_until_ms = now_ms + FORWARD_MONITOR_MS
                system_state = STATE_MOVE_MONITOR
                last_move_align_ms = now_ms
                move_timeout_reported = False
                print(f"[TASK {task_id}] 重新看到树，继续前进 area={target['area_pct']:.4f}")
                if should_display:
                    draw_status_overlay(img_yolo, "FAST RESTART")
                    dis.show(img_yolo)
                continue
            if decision["action"] != "align_confirm":
                print(f"[TASK {task_id}] {decision['message']}")

            if decision["next_state"] == STATE_DETECT:
                start_detect_window()
            elif not decision.get("send_packet", True):
                move_wait_until_ms = now_ms + decision["wait_ms"]
                system_state = decision["next_state"]
            else:
                send_motion_packet(
                    decision["x_dir"], decision["y_dir"],
                    decision["x_dist"], decision["y_dist"], decision["flag"]
                )
                move_wait_until_ms = now_ms + decision["wait_ms"]
                if decision["y_dir"] == MOVE_FORWARD_DIR and decision["y_dist"] > 0:
                    forward_start_guard_until_ms = now_ms + FORWARD_START_GUARD_MS
                    last_forward_resend_ms = now_ms
                    forward_resend_count = 0
                    y_forward_started = False
                system_state = decision["next_state"]
                if system_state == STATE_MOVE_MONITOR:
                    last_move_align_ms = now_ms
                    move_timeout_reported = False

        if should_display and system_state != STATE_DETECT:
            draw_status_overlay(img_yolo, decision["action"])
            dis.show(img_yolo)

    elif system_state == STATE_ALIGN_WAIT:
        if now_ms >= move_wait_until_ms:
            system_state = STATE_ALIGN
        elif should_display:
            draw_status_overlay(img_yolo, "ALIGNING")
            dis.show(img_yolo)

    elif system_state == STATE_MOVE_MONITOR:
        # 前进期间持续看绿色面积和横向位置；只有面积达到阈值才进入 YOLO。
        if live_target is not None:
            lost_target_count = 0
            loss_stop_sent = False
            update_area_filter(live_target["area_pct"])
            if area_reached_stop():
                print(
                    f"[APPROACH] 运动中面积到位 raw={live_target['area_pct']:.4f} "
                    f"med={last_filtered_area:.4f}"
                )
                start_detect_window()
            else:
                if now_ms < forward_start_guard_until_ms:
                    resent = resend_forward_command(now_ms)
                    last_decision_text = "Y START RESEND" if resent else "MOVING"
                else:
                    if correct_horizontal_while_moving(live_target, now_ms):
                        last_decision_text = "MOVE ALIGN"
                    else:
                        last_decision_text = "MOVING"
        else:
            forward_resent = resend_forward_command(now_ms)
            if now_ms < forward_start_guard_until_ms:
                if system_state == STATE_MOVE_MONITOR and should_display:
                    draw_status_overlay(img_yolo, "MOVING")
                    dis.show(img_yolo)
                continue
            lost_target_count += 1
            if lost_target_count == TARGET_LOST_LIMIT:
                stop_x_once()
                area_history.clear()
                last_filtered_area = -1.0
                print("[MOVE] 前进中暂时看不到树，继续等待面积到阈值或重新看到目标")

        if (
            system_state == STATE_MOVE_MONITOR
            and now_ms >= move_wait_until_ms
            and not move_timeout_reported
        ):
            move_timeout_reported = True
            print(
                f"[MOVE] 前进命令预计时间已到，但AREA未达到{STOP_AREA:.2f}，"
                "保持监控，不进入YOLO"
            )

        if system_state == STATE_MOVE_MONITOR and should_display:
            draw_status_overlay(img_yolo, "MOVING")
            dis.show(img_yolo)

    elif system_state == STATE_DETECT:
        if (frame_idx % DETECT_GREEN_SAMPLE_EVERY_N) == 0:
            detect_target = detect_has_valid_green_target(img_full)
            if detect_target:
                detect_last_green_ms = now_ms

        status = run_detect_cycle(img_full, img_yolo, should_display)

        no_green_ms = now_ms - detect_last_green_ms
        if no_green_ms >= DETECT_EXIT_NO_GREEN_MS:
            print(f"[DETECT] 连续 {no_green_ms}ms 未看到绿色，切换到下一棵树")
            finish_current_tree("no green in detect")
            status = "NEXT TREE"

        if should_display and system_state == STATE_DETECT:
            draw_detect_overlay(img_yolo, status)
            dis.show(img_yolo)

    fps = time.fps()
