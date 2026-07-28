# MaixCam2 AI 视觉模块

机器人的"眼睛"。用 YOLOv5 检测作物病虫害，同时通过 HSV 颜色分割引导机器人靠近目标。装在摄像头云台上，和 STM32 之间用 UART 通信。

## 硬件

- **模块**: Sipeed MaixCam2（AX630C NPU）
- **分辨率**: 摄像头输入 640×480，模型输入 320×224
- **通信**: UART4 (GPIO A21/A22) 接 STM32，WiFi 接基站

## 模型

用 MaixHub 训练的 YOLOv5，导出为：

| 文件 | 大小 | 说明 |
|------|------|------|
| `model_292463.mud` | <1 KB | 模型元数据 / 类别定义 |
| `model_292463_npu.axmodel` | 6.9 MB | NPU 推理模型 |
| `model_292463_vnpu.axmodel` | 7.3 MB | VNPU 备用模型 |

### 检测类别

| Class ID | Label | 中文 | 类型 |
|----------|-------|------|------|
| 0 | heban | 褐斑病 | 病害 |
| 1 | baifen | 白粉病 | 病害 |
| 2 | caiqingchong | 菜青虫 | 虫害 |
| 3 | woniu | 蜗牛 | 虫害 |

模型也支持猕猴桃和橘子的果实识别，用于地块作物类型匹配。

## 视觉引导流程

摄像头对每棵树执行多阶段引导：

1. **ALIGN** — 检测绿色植物区域，计算水平偏移量，发 X 轴修正指令给 STM32
2. **MOVE_MONITOR** — 前进过程中持续跟踪绿色区域面积，做横向微调保持居中
3. **DETECT** — 绿色面积超过阈值（~32%）时停下，连续跑 YOLO 推理
4. **UPLOAD** — 对每种病虫害保留最佳检测帧，上传到基站
5. **EXIT** — 发信号给 STM32 回撤，前往下一棵树

### 颜色定位

在 YOLO 运行之前，先用 HSV 色彩空间分割定位作物：

- 绿色通道提取（亮度和 delta 阈值过滤）
- 水平分层分解，区分不同植株
- 面积评分（中心距、尺寸、跟踪惩罚）
- 移动中值滤波抑制帧间抖动

相关参数都在 `main.py` 顶部定义，可以直接调。

## 与 STM32 的 UART 协议

```
AA AA x_dir y_dir x_dist_h x_dist_l y_dist_h y_dist_l flag FF FF

flag 含义：
  0 = 运动指令
  1 = 停下 / 目标锁定 / 开始检测
  2 = 目标丢失，停车
  3 = 距离过近，后退 / 安全停车
  4 = 本棵树完成，回撤
```

## 图片上传

上传到 `http://<BASE_STATION_IP>:8000/api/device/images`，multipart/form-data 格式：

```
POST /api/device/images
Content-Type: multipart/form-data

字段：
  event_id: string（需匹配已有的设备事件）
  pest_type: string（检测到的类别标签）
  file: image/jpeg
```

## 部署

1. 用 MaixHub 或 `maixcam2_7_26` 包烧录固件
2. 把 `main.py` 里的 `BASE_URL` 改成你的基站地址
3. 上传所有文件到设备
4. 重启 — 摄像头会等 STM32 的 READY 信号才启动

## 关键可调参数

`main.py` 顶部的几个参数直接影响行为：

- `DETECT_CONF_TH` (0.60) — YOLO 检测置信度阈值
- `STOP_AREA` (0.32) — 绿色占比触发停车的阈值
- `DETECT_EXIT_NO_GREEN_MS` (1200) — 连续无绿色多久判定树已完成
- `FORWARD_TRAVEL_DIST` (18) — 每棵树最大前进距离
- `UPLOAD_COOL` (5000) — 图片上传最小间隔 (ms)
