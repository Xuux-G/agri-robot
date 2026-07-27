# MaixCam2 AI Vision Module

YOLOv5-based pest detection and visual servoing on the Sipeed MaixCam2. Controls camera approach, identifies crop diseases in real time, and uploads annotated images to the base station.

## Hardware

- **Module**: Sipeed MaixCam2 (AX630C NPU)
- **Resolution**: 640x480 camera input, 320x224 model input
- **Interface**: UART4 (GPIO A21/A22) to STM32, WiFi to base station

## Model

Custom YOLOv5 model trained with MaixHub, exported as:

| File | Size | Purpose |
|------|------|---------|
| `model_292463.mud` | <1 KB | Model metadata / class definitions |
| `model_292463_npu.axmodel` | 6.9 MB | NPU-optimized inference model |
| `model_292463_vnpu.axmodel` | 7.3 MB | VNPU variant for fallback |

### Detection Classes

| Class ID | Label | Chinese | Category |
|----------|-------|---------|----------|
| 0 | heban | Brown Spot | Disease |
| 1 | baifen | Powdery Mildew | Disease |
| 2 | caiqingchong | Cabbage Worm | Pest |
| 3 | woniu | Snail | Pest |
| — | mihoutao | Kiwi | Fruit (for block mapping) |
| — | juzi | Orange | Fruit (for block mapping) |

## Visual Servoing Pipeline

The camera executes a multi-stage approach for each crop:

1. **ALIGN** — Detect green plant mass, compute horizontal offset, send X-axis correction to STM32
2. **MOVE_MONITOR** — Advance forward while tracking green area; lateral micro-corrections to stay centered
3. **DETECT** — Stop when green area exceeds threshold (~32%), run continuous YOLO inference
4. **UPLOAD** — For each pest class, track the best detection frame and upload to base station
5. **EXIT** — Signal STM32 to retract and move to next tree

### Color-based Targeting

Before YOLO runs, the system uses HSV color segmentation to locate crops:

- Green channel extraction with luminance/delta thresholds
- Horizontal band decomposition to separate individual plants
- Area-based scoring (proximity to center, size, tracking penalty)
- Moving median filter to suppress frame-to-frame jitter

Key tunable parameters are defined at the top of `main.py`.

## UART Protocol (to STM32)

```
AA AA x_dir y_dir x_dist_h x_dist_l y_dist_h y_dist_l flag FF FF

flag values:
  0 = motion command
  1 = stop / target acquired / begin detection
  2 = target lost, stop
  3 = too close, back off / safety stop
  4 = tree complete, retract
```

## Base Station Upload

Images are uploaded to `http://<BASE_STATION_IP>:8000/api/device/images` as multipart/form-data:

```
POST /api/device/images
Content-Type: multipart/form-data

Fields:
  event_id: string (must match existing device event)
  pest_type: string (detected class label)
  file: image/jpeg
```

## Setup

1. Flash firmware via MaixHub or `maixcam2_7_26` package
2. Edit `BASE_URL` in `main.py` to point to your base station
3. Upload all files to the device
4. Reboot — the camera waits for STM32 READY signal before starting

## Configuration Reference

All behavioral parameters are in the top section of `main.py`:

- `DETECT_CONF_TH` (0.60) — confidence threshold for pest detection
- `STOP_AREA` (0.32) — green fill ratio that triggers stop-and-detect
- `DETECT_EXIT_NO_GREEN_MS` (1200) — timeout before concluding tree is done
- `FORWARD_TRAVEL_DIST` (18) — max forward travel per tree
- `UPLOAD_COOL` (5000) — minimum interval between image uploads (ms)
