# 基站

基站是整个系统的中枢。它接收 ESP32 的设备事件和 MaixCam2 的图片上传，根据病虫害类型和农药安全间隔期计算配药处方，然后通过 API 把指令分发回 ESP32 执行。前端仪表盘用于农场的实时状态监控和管理。

## 技术栈

- **后端**: Python 3.10+ / FastAPI / SQLite（原生 SQL，没用 ORM）
- **前端**: Vue 3 (Composition API) / Vite / Vue Router / Tailwind CSS

## 快速启动

```bash
# 安装依赖
pip install -r requirements.txt

# 启动后端（端口 8000）
uvicorn app:app --host 0.0.0.0 --reload
```

另开一个终端：

```bash
cd frontend
npm install
npm run dev        # 开发服务器 :5173，/api 代理到 :8000
```

浏览器打开 `http://localhost:5173`。

## API 说明

### 设备通信

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/device/events` | POST | ESP32 上报事件（NFC 读取、喷洒完成等）|
| `/api/device/images` | POST | MaixCam2 上传图片（multipart/form-data）|
| `/api/device/command` | GET | ESP32 轮询待执行的处方指令 |

### 农场管理

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/farm/status` | GET | 农场整体统计数据 |
| `/api/farm/plots` | GET | 所有地块状态列表 |
| `/api/farm/plots/{block_id}/detail` | GET | 单地块历史记录 + 图片 |
| `/api/farm/plots/{block_id}/prescription` | GET | 配药方案（含安全间隔期检查）|
| `/api/farm/weather` | GET | 温湿度数据 |
| `/api/dashboard/overview` | GET | 仪表盘汇总 |

### 模拟调试接口

以下接口用于开发和演示，模拟设备行为：

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/nfc/read` | POST | 模拟 NFC 读取 |
| `/api/detection/submit` | POST | 模拟病虫害检测结果提交 |
| `/api/command/dispatch` | POST | 模拟指令下发 |
| `/api/nfc/writeback` | POST | 模拟数据回写 |
| `/api/plots/{plot_code}/history` | GET | 按地块编码查询历史 |

## 数据库

建表语句见 [`schema.sql`](schema.sql)。主要表结构：

- `plots` — 地块基础信息
- `device_events` — 设备事件日志（靠 `event_id` 做幂等去重）
- `pest_records` — 病虫害检测记录
- `operation_logs` — 喷洒/施肥/除草操作日志
- `pesticides` — 农药库存（含安全间隔期 PHI 数据）
- `inventory` — 库存管理

首次启动时自动建表，后续迁移通过 `ALTER TABLE ADD COLUMN` 做热升级，不丢数据。

## 前端页面

| 路由 | 页面 | 说明 |
|------|------|------|
| `/` | Dashboard | 统计概览、病虫害排名、地块增删管理 |
| `/monitor` | Live Monitor | 6 宫格数字孪生 + 三通道泵混合动画 |
| `/archives` | Archives | 农场管理表格 + 处方时间线 |
| `/knowledge` | Knowledge Base | 病虫害知识库，支持搜索和详情展开 |
| `/plot/:code` | Plot Detail | 单地块历史记录查看 |

## 几个设计点

- **事件幂等**：每个设备事件都带唯一 `event_id`，设备重试用同一个 ID，新事件必须用新 ID。数据库 UNIQUE 约束保证不会重复处理。
- **地块编码**：格式为 `A-01`、`A-02` 这样的编码，`A01` 和 `A-01` 两种写法都能正确处理（自动标准化）。
- **PHI 安全锁**：处方计算时会检查农药安全间隔期，同一地块在间隔期内不能重复使用同机制农药，防止农残超标。

## 环境变量

- `BASE_STATION_TOKEN` — 可选，设置后 ESP32 需要在请求头中携带 `X-Device-Token` 才能调用写接口
