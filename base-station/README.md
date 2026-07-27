# Base Station

The central server and management dashboard for the Agri Robot system. Receives device events, processes pest detection data, computes dosing prescriptions, and serves a real-time farm monitoring UI.

## Stack

- **Backend**: Python 3.10+ / FastAPI / SQLite (raw SQL, no ORM)
- **Frontend**: Vue 3 (Composition API) / Vite / Vue Router / Tailwind CSS

## Quick Start

```bash
# Install dependencies
pip install -r requirements.txt

# Start backend (port 8000)
uvicorn app:app --host 0.0.0.0 --reload
```

In a separate terminal:

```bash
cd frontend
npm install
npm run dev        # dev server at :5173, proxies /api to :8000
```

Open `http://localhost:5173` in a browser.

## API Overview

### Device Communication

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/device/events` | POST | ESP32 event upload (NFC read, spray complete, etc.) |
| `/api/device/images` | POST | MaixCam2 image upload (multipart/form-data) |
| `/api/device/command` | GET | ESP32 polls for pending prescriptions |

### Farm Management

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/farm/status` | GET | Aggregate farm statistics |
| `/api/farm/plots` | GET | All block statuses |
| `/api/farm/plots/{block_id}/detail` | GET | Single block history + images |
| `/api/farm/plots/{block_id}/prescription` | GET | Dosing plan with PHI safety lock |
| `/api/farm/weather` | GET | Weather data (temperature, humidity) |
| `/api/dashboard/overview` | GET | Dashboard summary |

### Demo Simulation (legacy)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/nfc/read` | POST | Simulated NFC tag read |
| `/api/detection/submit` | POST | Simulated pest detection |
| `/api/command/dispatch` | POST | Simulated command dispatch |
| `/api/nfc/writeback` | POST | Simulated write-back |
| `/api/plots/{plot_code}/history` | GET | Plot history query |

## Database

The schema is defined in [`schema.sql`](schema.sql). Key tables:

- `plots` — plot/block metadata
- `device_events` — raw device events (idempotent by `event_id`)
- `pest_records` — pest detection history
- `operation_logs` — spray/fertilize/weed operations
- `pesticides` — pesticide inventory with PHI intervals
- `inventory` — stock levels

Database auto-creates on first run. Migrations use `ALTER TABLE ADD COLUMN` for hot-upgrades.

## Dashboard Pages

| Route | View | Description |
|-------|------|-------------|
| `/` | Dashboard | Stats overview, pest ranking, block CRUD |
| `/monitor` | Live Monitor | 6-grid digital twin + 3-channel pump mixing animation |
| `/archives` | Archives | Farm management table with prescription timeline |
| `/knowledge` | Knowledge Base | Pest/disease library with search + detail panels |
| `/plot/:code` | Plot Detail | Legacy single-plot history view |

## Design Notes

- **Event Idempotency**: Every device event carries a unique `event_id`. Retries use the same ID; new events must use a new ID. Backend enforces this via UNIQUE constraint.
- **Block ID Format**: `A-01`, `A-02`, etc. Both `A01` and `A-01` are accepted (auto-normalized).
- **PHI Safety Lock**: Prescriptions enforce pre-harvest interval compliance. Same-mechanism pesticides on the same block within the PHI window are blocked.

## Environment Variables

- `BASE_STATION_TOKEN` — optional write token for protected endpoints (set on ESP32 as `X-Device-Token` header)
