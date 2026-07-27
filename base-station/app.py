from datetime import date, datetime
import os
from pathlib import Path
import re
import shutil
import asyncio
import contextlib
import sqlite3
import threading
from typing import Literal, Optional
from uuid import uuid4
import httpx
import time

from fastapi import Depends, FastAPI, File, Form, Header, HTTPException, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

DB_PATH = Path(__file__).parent / "agri_robot.db"
UPLOAD_DIR = Path(__file__).parent / "uploads"

# Farm coordinates for weather (Default: Nanjing)
FARM_LATITUDE = 32.06
FARM_LONGITUDE = 118.80
_weather_cache = {"data": None, "timestamp": 0}
_weather_lock = asyncio.Lock()

# ESP32 Hub State
_hub_pending = {}
_hub_lock = threading.Lock()
MAX_UPLOAD_BYTES = int(os.getenv("MAX_UPLOAD_BYTES", str(5 * 1024 * 1024)))
WRITE_API_TOKEN = os.getenv("BASE_STATION_TOKEN", "").strip()

app = FastAPI(
    title="Smart Agri Robot API",
    description="NFC + vision + operation logging backend for competition demos.",
    version="0.1.0",
)

UPLOAD_DIR.mkdir(parents=True, exist_ok=True)
app.mount("/uploads", StaticFiles(directory=str(UPLOAD_DIR)), name="uploads")

app.add_middleware(
    CORSMiddleware,
    allow_origins=[
        "http://127.0.0.1:5173",
        "http://localhost:5173",
    ],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


def now_iso() -> str:
    return datetime.now().isoformat(timespec="seconds")


ACTIONABLE_PEST_TYPES = {
    "白粉病",
    "褐斑病",
    "菜青虫",
    "蜗牛",
    "褐斑病+菜青虫",
}
BLOCK_EXPECTED_PESTS = {
    "A-01": {"菜青虫"},
    "A-02": {"白粉病"},
    "A-03": {"褐斑病", "菜青虫", "褐斑病+菜青虫"},
    "A-04": {"蜗牛"},
    "A-05": set(),
    "A-06": {"褐斑病"},
}
BLOCK_EXPECTED_FRUITS = {
    "A-04": {"猕猴桃", "mihoutao"},
    "A-06": {"橘子", "juzi"},
}
MAIXCAM_PEST_TYPE_MAP = {
    "baifen": "白粉病",
    "heban": "褐斑病",
    "caiqingchong": "菜青虫",
    "woniu": "蜗牛",
    "heban+caiqingchong": "褐斑病+菜青虫",
    "mihoutao": "猕猴桃",
    "juzi": "橘子",
}
DEFAULT_HUB_ROBOT_ID = "esp32-s3-hub-001"
TASK_STATUS_PENDING = "待执行"
TASK_STATUS_RUNNING = "执行中"
TASK_STATUS_COMPLETED = "已完成"


def is_actionable_pest_type(pest_type: str) -> bool:
    return (pest_type or "").strip() in ACTIONABLE_PEST_TYPES


def is_expected_block_pest(block_id: str, pest_type: str) -> bool:
    block_display = to_block_id_display(normalize_block_code(block_id))
    pest_name = (pest_type or "").strip()
    if not is_actionable_pest_type(pest_name):
        return True
    return pest_name in BLOCK_EXPECTED_PESTS.get(block_display, set())


def is_expected_block_fruit(block_id: str, fruit_type: str) -> bool:
    block_display = to_block_id_display(normalize_block_code(block_id))
    fruit_name = (fruit_type or "").strip()
    if fruit_name not in {"猕猴桃", "橘子", "mihoutao", "juzi"}:
        return True
    return fruit_name in BLOCK_EXPECTED_FRUITS.get(block_display, set())


def get_recent_hub_block(conn: sqlite3.Connection, robot_id: str) -> str:
    safe_robot_id = (robot_id or "").strip() or DEFAULT_HUB_ROBOT_ID
    event = conn.execute(
        """
        SELECT block_id
        FROM device_events
        WHERE robot_id = ?
                    AND (event_id LIKE 'nfc_detected-%' OR event_id LIKE 'nfc-detected-%')
                    AND status = 'running'
          AND datetime(created_at) >= datetime('now', 'localtime', '-5 minutes')
        ORDER BY id DESC
        LIMIT 1
        """,
        (safe_robot_id,),
    ).fetchone()
    if event and event["block_id"]:
        return event["block_id"]

    robot = conn.execute(
        """
        SELECT location_plot_code
        FROM robot_status
        WHERE robot_id = ?
        ORDER BY updated_at DESC
        LIMIT 1
        """,
        (safe_robot_id,),
    ).fetchone()
    if robot and robot["location_plot_code"]:
        return to_block_id_display(robot["location_plot_code"])
    return ""


def merge_block_pest_types(block_display: str, pest_names: list[str]) -> str:
    names = [name for name in pest_names if is_actionable_pest_type(name)]
    if not names:
        return ""
    if block_display == "A-03":
        name_set = set(names)
        if "褐斑病" in name_set and "菜青虫" in name_set:
            return "褐斑病+菜青虫"
    return names[0]


def ensure_actionable_pest_record(
    conn: sqlite3.Connection,
    *,
    plot_id: int,
    event_id: str,
    block_id: str,
    pest_type: str,
    detected_at: str,
    image_url: str,
    severity_score: float,
) -> tuple[str, Optional[int]]:
    pest_name = (pest_type or "").strip()
    if not is_actionable_pest_type(pest_name):
        return ("skipped_non_actionable", None)

    existing = conn.execute(
        """
        SELECT id
        FROM pest_records
        WHERE source_event_id = ?
        ORDER BY id DESC
        LIMIT 1
        """,
        (event_id,),
    ).fetchone()

    if existing:
        conn.execute(
            """
            UPDATE pest_records
            SET plot_id = ?,
                detected_at = ?,
                pest_type = ?,
                severity = ?,
                image_url = ?,
                task_status = ?,
                source_event_id = ?
            WHERE id = ?
            """,
            (
                plot_id,
                detected_at,
                pest_name,
                severity_score,
                image_url,
                TASK_STATUS_PENDING,
                event_id,
                existing["id"],
            ),
        )
        print(
            f"[DEVICE_IMAGE] pest_record reused id={existing['id']} "
            f"event_id={event_id} block={block_id} pest={pest_name}"
        )
        return ("reused", existing["id"])

    cur = conn.execute(
        """
        INSERT INTO pest_records (
            plot_id, detected_at, pest_type, severity,
            model_name, image_url, task_status, crop_category,
            pesticide_type, source_event_id
        ) VALUES (?, ?, ?, ?, 'maixcam2-device', ?, ?, '', '', ?)
        """,
        (
            plot_id,
            detected_at,
            pest_name,
            severity_score,
            image_url,
            TASK_STATUS_PENDING,
            event_id,
        ),
    )
    print(
        f"[DEVICE_IMAGE] pest_record created id={cur.lastrowid} "
        f"event_id={event_id} block={block_id} pest={pest_name}"
    )
    return ("created", cur.lastrowid)


def require_write_token(x_device_token: Optional[str] = Header(default=None)) -> None:
    if WRITE_API_TOKEN and x_device_token != WRITE_API_TOKEN:
        raise HTTPException(status_code=401, detail="invalid device token")


def is_supported_image(data: bytes) -> bool:
    if data.startswith(b"\xff\xd8\xff"):
        return True
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return True
    return data.startswith(b"RIFF") and data[8:12] == b"WEBP"


@contextlib.contextmanager
def get_conn():
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    try:
        yield conn
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


def init_db() -> None:
    with get_conn() as conn:
        conn.execute("PRAGMA journal_mode=WAL")
        conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS plots (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                code TEXT NOT NULL UNIQUE,
                name TEXT NOT NULL,
                area_m2 REAL DEFAULT 0,
                current_crop_id INTEGER,
                created_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS nfc_tags (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                uid TEXT NOT NULL UNIQUE,
                plot_id INTEGER NOT NULL,
                installed_at TEXT NOT NULL,
                status TEXT NOT NULL DEFAULT 'active',
                FOREIGN KEY (plot_id) REFERENCES plots(id)
            );

            CREATE TABLE IF NOT EXISTS crop_cycles (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                plot_id INTEGER NOT NULL,
                variety TEXT NOT NULL,
                planted_at TEXT NOT NULL,
                expected_harvest_at TEXT,
                status TEXT NOT NULL DEFAULT 'growing',
                notes TEXT,
                FOREIGN KEY (plot_id) REFERENCES plots(id)
            );

            CREATE TABLE IF NOT EXISTS pest_records (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                plot_id INTEGER NOT NULL,
                detected_at TEXT NOT NULL,
                pest_type TEXT NOT NULL,
                severity REAL NOT NULL,
                model_name TEXT,
                image_url TEXT,
                handled INTEGER NOT NULL DEFAULT 0,
                FOREIGN KEY (plot_id) REFERENCES plots(id)
            );

            CREATE TABLE IF NOT EXISTS operation_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                plot_id INTEGER NOT NULL,
                action_type TEXT NOT NULL,
                amount REAL,
                unit TEXT,
                reason TEXT,
                result TEXT NOT NULL DEFAULT 'done',
                operator TEXT NOT NULL DEFAULT 'robot',
                created_at TEXT NOT NULL,
                FOREIGN KEY (plot_id) REFERENCES plots(id)
            );

            CREATE TABLE IF NOT EXISTS inventory (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                item_type TEXT NOT NULL,
                item_name TEXT NOT NULL UNIQUE,
                stock REAL NOT NULL DEFAULT 0,
                unit TEXT NOT NULL,
                low_threshold REAL NOT NULL DEFAULT 0
            );

            CREATE TABLE IF NOT EXISTS pesticides (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                type TEXT NOT NULL,
                base_dosage REAL NOT NULL,
                phi_days INTEGER NOT NULL DEFAULT 0
            );

            CREATE TABLE IF NOT EXISTS robot_status (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                robot_id TEXT NOT NULL UNIQUE,
                battery_level REAL NOT NULL DEFAULT 100,
                pesticide_level REAL NOT NULL DEFAULT 0,
                fertilizer_level REAL NOT NULL DEFAULT 0,
                location_plot_code TEXT,
                status TEXT NOT NULL DEFAULT 'idle',
                updated_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS device_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                event_uuid TEXT,
                event_id TEXT NOT NULL UNIQUE,
                robot_id TEXT NOT NULL,
                plot_id INTEGER NOT NULL,
                block_id TEXT NOT NULL,
                crop_type TEXT NOT NULL DEFAULT '',
                pest_type TEXT NOT NULL DEFAULT 'none',
                action_type TEXT NOT NULL DEFAULT 'none',
                severity TEXT NOT NULL DEFAULT '正常',
                status TEXT NOT NULL DEFAULT 'idle',
                temperature REAL,
                humidity REAL,
                device_time TEXT,
                image_url TEXT,
                created_at TEXT NOT NULL,
                FOREIGN KEY (plot_id) REFERENCES plots(id)
            );

            CREATE INDEX IF NOT EXISTS idx_device_events_plot_id ON device_events(plot_id);
            CREATE INDEX IF NOT EXISTS idx_device_events_created_at ON device_events(created_at);
            """
        )
        columns = {
            row["name"]
            for row in conn.execute("PRAGMA table_info(pest_records)").fetchall()
        }
        if "task_status" not in columns:
            conn.execute(
                "ALTER TABLE pest_records ADD COLUMN task_status TEXT NOT NULL DEFAULT '待执行'"
            )
        if "crop_category" not in columns:
            conn.execute(
                "ALTER TABLE pest_records ADD COLUMN crop_category TEXT NOT NULL DEFAULT ''"
            )
        if "pesticide_type" not in columns:
            conn.execute(
                "ALTER TABLE pest_records ADD COLUMN pesticide_type TEXT NOT NULL DEFAULT ''"
            )

        if "source_event_id" not in columns:
            conn.execute(
                "ALTER TABLE pest_records ADD COLUMN source_event_id TEXT"
            )

        if "pesticide_id" not in columns:
            conn.execute(
                "ALTER TABLE pest_records ADD COLUMN pesticide_id INTEGER"
            )

        if "action_detail" not in columns:
            conn.execute(
                "ALTER TABLE pest_records ADD COLUMN action_detail TEXT"
            )

        op_columns = {
            row["name"]
            for row in conn.execute("PRAGMA table_info(operation_logs)").fetchall()
        }
        if "source_event_id" not in op_columns:
            conn.execute(
                "ALTER TABLE operation_logs ADD COLUMN source_event_id TEXT"
            )

        device_columns = {
            row["name"]
            for row in conn.execute("PRAGMA table_info(device_events)").fetchall()
        }
        if "event_uuid" not in device_columns:
            conn.execute("ALTER TABLE device_events ADD COLUMN event_uuid TEXT")

        seed_demo_data(conn)


def seed_demo_data(conn: sqlite3.Connection) -> None:
    created_at = now_iso()
    demo_plots = [
        ("A01", "1号温室地块", 120.5, "辣椒-陇椒系列", "2026-03-20", "2026-06-20"),
        ("A02", "2号温室地块", 98.0, "茶树-龙井43", "2026-03-28", "2026-06-30"),
        ("A03", "3号温室地块", 105.0, "石榴-突尼斯软籽", "2026-04-02", "2026-05-25"),
        ("A04", "4号温室地块", 112.0, "猕猴桃-徐香", "2026-04-05", "2026-07-10"),
        ("A05", "5号温室地块", 95.0, "山茶花-十八学士", "2026-04-08", "2026-07-15"),
        ("A06", "6号温室地块", 88.0, "柑橘-沃柑", "2026-04-12", "2026-06-18"),
    ]

    for code, name, area_m2, *_ in demo_plots:
        conn.execute(
            """
            INSERT INTO plots (code, name, area_m2, created_at)
            SELECT ?, ?, ?, ?
            WHERE NOT EXISTS (SELECT 1 FROM plots WHERE code = ?)
            """,
            (code, name, area_m2, created_at, code),
        )

    plot_rows = conn.execute(
        "SELECT id, code FROM plots WHERE code BETWEEN 'A01' AND 'A06'"
    ).fetchall()
    plot_id_by_code = {row["code"]: row["id"] for row in plot_rows}

    for code, _, _, variety, planted_at, expected_harvest_at in demo_plots:
        plot_id = plot_id_by_code.get(code)
        if not plot_id:
            continue
        nfc_uid = f"NFC-{code}"
        conn.execute(
            """
            INSERT INTO nfc_tags (uid, plot_id, installed_at)
            SELECT ?, ?, ?
            WHERE NOT EXISTS (SELECT 1 FROM nfc_tags WHERE uid = ?)
            """,
            (nfc_uid, plot_id, created_at, nfc_uid),
        )
        conn.execute(
            """
            INSERT INTO crop_cycles (plot_id, variety, planted_at, expected_harvest_at, notes)
            SELECT ?, ?, ?, ?, '演示种植周期'
            WHERE NOT EXISTS (SELECT 1 FROM crop_cycles WHERE plot_id = ?)
            """,
            (plot_id, variety, planted_at, expected_harvest_at, plot_id),
        )
        # 已存在的种植周期同步为最新品种
        conn.execute(
            "UPDATE crop_cycles SET variety = ? WHERE plot_id = ? AND status = 'growing'",
            (variety, plot_id),
        )

    inventory_items = [
        ("pesticide", "吡虫啉", 8.5, "L", 2),
        ("fertilizer", "复合肥NPK", 25.0, "kg", 5),
    ]
    for item_type, item_name, stock, unit, low_threshold in inventory_items:
        conn.execute(
            """
            INSERT INTO inventory (item_type, item_name, stock, unit, low_threshold)
            SELECT ?, ?, ?, ?, ?
            WHERE NOT EXISTS (SELECT 1 FROM inventory WHERE item_name = ?)
            """,
            (item_type, item_name, stock, unit, low_threshold, item_name),
        )

    pesticide_items = [
        ("毒死蜱", "有机磷类", 1.5, 14),
        ("吡虫啉", "新烟碱类", 0.5, 7),
        ("阿维菌素", "大环内酯类", 0.8, 7),
        ("代森锰锌", "二硫代氨基甲酸酯类", 2.0, 15),
    ]
    for name, type_name, base_dosage, phi_days in pesticide_items:
        conn.execute(
            """
            INSERT INTO pesticides (name, type, base_dosage, phi_days)
            SELECT ?, ?, ?, ?
            WHERE NOT EXISTS (SELECT 1 FROM pesticides WHERE name = ?)
            """,
            (name, type_name, base_dosage, phi_days, name),
        )

    conn.execute(
        """
        INSERT INTO robot_status (
            robot_id, battery_level, pesticide_level, fertilizer_level, location_plot_code, status, updated_at
        )
        SELECT 'car-01', 96, 4, 10, 'A01', 'idle', ?
        WHERE NOT EXISTS (SELECT 1 FROM robot_status WHERE robot_id = 'car-01')
        """,
        (created_at,),
    )
    conn.execute(
        """
        INSERT INTO robot_status (
            robot_id, battery_level, pesticide_level, fertilizer_level, location_plot_code, status, updated_at
        )
        SELECT 'agricultural robot', 96, 4, 10, 'A01', 'idle', ?
        WHERE NOT EXISTS (SELECT 1 FROM robot_status WHERE robot_id = 'agricultural robot')
        """,
        (created_at,),
    )

    # ---- 初始 device_events（每次启动刷新为最新，确保 Archives 初始状态一致）----
    seed_events = [
        # (event_id, plot_code, pest_type, severity, action_type, status)
        ("seed-init-A01", "A01", "白粉病",   "轻度", "spray", "idle"),
        ("seed-init-A02", "A02", "none",    "正常", "none",  "idle"),
        ("seed-init-A03", "A03", "none",    "正常", "none",  "idle"),
        ("seed-init-A04", "A04", "蜗牛",   "轻度", "spray", "idle"),
        ("seed-init-A05", "A05", "none",    "轻度", "none",  "idle"),
        ("seed-init-A06", "A06", "褐斑病",   "轻度", "spray", "idle"),
    ]
    for event_id, plot_code, pest_type, severity, action_type, status in seed_events:
        pid = plot_id_by_code.get(plot_code)
        if not pid:
            continue
        # 删除旧种子事件后重插，确保排在最新
        conn.execute("DELETE FROM device_events WHERE event_id = ?", (event_id,))
        conn.execute(
            """
            INSERT INTO device_events (
                event_uuid, event_id, robot_id, plot_id, block_id,
                crop_type, pest_type, action_type, severity, status,
                image_url, created_at
            )
            VALUES (?, ?, 'seed-demo', ?, ?,
                    (SELECT variety FROM crop_cycles WHERE plot_id = ? ORDER BY id DESC LIMIT 1),
                    ?, ?, ?, ?,
                    NULL, ?)
            """,
            (
                str(uuid4()), event_id, pid, to_block_id_display(plot_code),
                pid, pest_type, action_type, severity, status, created_at,
            ),
        )


class NfcReadRequest(BaseModel):
    nfc_uid: str = Field(..., examples=["NFC-A01"])


class DetectionSubmitRequest(BaseModel):
    nfc_uid: str
    pest_type: str
    severity: float = Field(..., ge=0, le=1)
    model_name: str = "yolo-nano"
    image_url: Optional[str] = None
    event_type: str = ""


class CommandRequest(BaseModel):
    nfc_uid: str
    action_type: Literal["weed", "fertilize", "spray"]
    amount: Optional[float] = None
    unit: Optional[str] = None
    reason: Optional[str] = None


class OperationWritebackRequest(BaseModel):
    nfc_uid: str
    action_type: Literal["weed", "fertilize", "spray"]
    amount: Optional[float] = None
    unit: Optional[str] = None
    result: Literal["done", "partial", "failed"] = "done"
    reason: Optional[str] = None
    operator: str = "robot"


SeverityLabel = Literal["正常", "轻度", "严重"]
TaskStatus = Literal["待执行", "执行中", "已完成"]
DeviceStatus = Literal["idle", "running", "completed", "error"]


class DetectionCrudPayload(BaseModel):
    block_id: str = Field(..., examples=["A01"])
    pest_type: str
    severity: SeverityLabel
    handled_at: str
    status: TaskStatus
    crop_category: Optional[str] = None
    pesticide_type: Optional[str] = None


class DeviceEventPayload(BaseModel):
    event_id: str = Field(..., examples=["CAR01-0001"])
    robot_id: str = "agricultural robot"
    block_id: str = Field(..., examples=["A-01"])
    event_type: str = ""
    crop_type: str = ""
    pest_type: str = "none"
    action_type: str = "none"
    severity: str = "正常"
    status: str = "idle"
    device_time: Optional[str] = None
    temperature: Optional[float] = None
    humidity: Optional[float] = None
    image_url: Optional[str] = None


def severity_label_to_score(label: SeverityLabel) -> float:
    return {"正常": 0.3, "轻度": 0.6, "严重": 0.9}[label]


def severity_score_to_label(score: float) -> SeverityLabel:
    if score >= 0.75:
        return "严重"
    if score >= 0.45:
        return "轻度"
    return "正常"


def normalize_block_code(block_id: str) -> str:
    cleaned = block_id.strip().upper().replace(" ", "")
    matched = re.fullmatch(r"([A-Z]+)-?(\d{2})", cleaned)
    if not matched:
        raise HTTPException(
            status_code=422,
            detail="block_id format invalid, expected like A-01",
        )
    return f"{matched.group(1)}{matched.group(2)}"


def to_block_id_display(plot_code: str) -> str:
    matched = re.fullmatch(r"([A-Z]+)(\d{2})", plot_code.strip().upper())
    if not matched:
        return plot_code
    return f"{matched.group(1)}-{matched.group(2)}"


def normalize_severity_label(raw: str) -> SeverityLabel:
    cleaned = raw.strip().lower()
    mapping = {
        "正常": "正常",
        "轻度": "轻度",
        "严重": "严重",
        "normal": "正常",
        "low": "正常",
        "medium": "轻度",
        "high": "严重",
    }
    if cleaned not in mapping:
        raise HTTPException(
            status_code=422,
            detail="severity invalid, expected 正常/轻度/严重",
        )
    return mapping[cleaned]


def normalize_device_status(raw: str) -> DeviceStatus:
    cleaned = raw.strip().lower()
    mapping = {
        "idle": "idle",
        "running": "running",
        "completed": "completed",
        "error": "error",
        "待执行": "idle",
        "执行中": "running",
        "已完成": "completed",
        "异常": "error",
    }
    if cleaned not in mapping:
        raise HTTPException(
            status_code=422,
            detail="status invalid, expected idle/running/completed/error",
        )
    return mapping[cleaned]


def status_to_task_status(status: DeviceStatus) -> TaskStatus:
    if status == "running":
        return TASK_STATUS_RUNNING
    if status == "completed":
        return TASK_STATUS_COMPLETED
    return TASK_STATUS_PENDING


def status_to_display(status: DeviceStatus) -> str:
    return {
        "idle": "待机",
        "running": "执行中",
        "completed": "已完成",
        "error": "异常",
    }[status]


def get_plot_by_uid(conn: sqlite3.Connection, nfc_uid: str) -> sqlite3.Row:
    row = conn.execute(
        """
        SELECT p.*
        FROM plots p
        JOIN nfc_tags n ON n.plot_id = p.id
        WHERE n.uid = ?
        """,
        (nfc_uid,),
    ).fetchone()
    if not row:
        raise HTTPException(status_code=404, detail=f"NFC tag not found: {nfc_uid}")
    return row


def get_plot_by_code(conn: sqlite3.Connection, block_id: str) -> sqlite3.Row:
    normalized_code = normalize_block_code(block_id)
    row = conn.execute(
        "SELECT * FROM plots WHERE code = ?",
        (normalized_code,),
    ).fetchone()
    if not row:
        raise HTTPException(
            status_code=404,
            detail=f"plot not found: {to_block_id_display(normalized_code)}",
        )
    return row


def upsert_robot_status(
    conn: sqlite3.Connection,
    robot_id: str,
    plot_code: str,
    status: DeviceStatus,
) -> None:
    now = now_iso()
    existed = conn.execute(
        "SELECT id FROM robot_status WHERE robot_id = ?",
        (robot_id,),
    ).fetchone()

    if existed:
        conn.execute(
            """
            UPDATE robot_status
            SET location_plot_code = ?, status = ?, updated_at = ?
            WHERE robot_id = ?
            """,
            (plot_code, status, now, robot_id),
        )
        return

    conn.execute(
        """
        INSERT INTO robot_status (
            robot_id, battery_level, pesticide_level, fertilizer_level, location_plot_code, status, updated_at
        )
        VALUES (?, 100, 0, 0, ?, ?, ?)
        """,
        (robot_id, plot_code, status, now),
    )


@app.on_event("startup")
def on_startup() -> None:
    init_db()


@app.get("/health")
def health() -> dict:
    return {"ok": True, "time": now_iso()}


@app.post("/api/device/events")
def ingest_device_event(
    payload: DeviceEventPayload,
    _: None = Depends(require_write_token),
) -> dict:
    event_id = payload.event_id.strip()
    if not event_id:
        raise HTTPException(status_code=422, detail="event_id is required")

    robot_id = payload.robot_id.strip() or "agricultural robot"
    status = normalize_device_status(payload.status)
    severity = normalize_severity_label(payload.severity)
    action_type = (payload.action_type or "none").strip() or "none"
    pest_type = (payload.pest_type or "none").strip() or "none"
    crop_type = (payload.crop_type or "").strip()
    image_url = (payload.image_url or "").strip() or None
    event_time = (payload.device_time or "").strip() or now_iso()
    created_at = now_iso()

    with get_conn() as conn:
        duplicated = conn.execute(
            "SELECT id, created_at, block_id FROM device_events WHERE event_id = ?",
            (event_id,),
        ).fetchone()

        # Hub: track NFC block detection before MaixCam image upload arrives.
        event_type = payload.event_type or ""
        if event_type == "nfc_detected":
            normalized_block = normalize_block_code(payload.block_id)
            block_num = int(normalized_block[-2:])
            with _hub_lock:
                _hub_pending[robot_id] = {
                    "block": block_num,
                    "pest_type": None,
                    "pesticide": None,
                    "updated_at": time.time(),
                }

        if duplicated:
            # 即使事件重复，仍更新机器人位置，确保前端小车能跳转
            try:
                plot = get_plot_by_code(conn, payload.block_id)
                upsert_robot_status(conn, robot_id, plot["code"], status)
            except Exception:
                pass
            return {
                "ok": True,
                "duplicate": True,
                "event_id": event_id,
                "block_id": duplicated["block_id"],
                "created_at": duplicated["created_at"],
                "message": "duplicate event_id ignored; use a new event_id for a new report",
            }

        plot = get_plot_by_code(conn, payload.block_id)

        previous_status = conn.execute(
            "SELECT status FROM device_events WHERE plot_id = ? ORDER BY id DESC LIMIT 1",
            (plot["id"],),
        ).fetchone()
        if previous_status and previous_status["status"] == "idle" and status == "completed":
            raise HTTPException(
                status_code=422,
                detail="invalid status transition: idle -> completed",
            )

        event_uuid = str(uuid4())
        cur = conn.execute(
            """
            INSERT INTO device_events (
                event_uuid,
                event_id,
                robot_id,
                plot_id,
                block_id,
                crop_type,
                pest_type,
                action_type,
                severity,
                status,
                temperature,
                humidity,
                device_time,
                image_url,
                created_at
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                event_uuid,
                event_id,
                robot_id,
                plot["id"],
                to_block_id_display(plot["code"]),
                crop_type,
                pest_type,
                action_type,
                severity,
                status,
                payload.temperature,
                payload.humidity,
                payload.device_time,
                image_url,
                created_at,
            ),
        )

        upsert_robot_status(conn, robot_id, plot["code"], status)

        if pest_type.lower() != "none":
            conn.execute(
                """
                INSERT INTO pest_records (
                    plot_id,
                    detected_at,
                    pest_type,
                    severity,
                    model_name,
                    image_url,
                    task_status,
                    crop_category,
                    pesticide_type,
                    source_event_id
                )
                VALUES (?, ?, ?, ?, 'maixcam2-device', ?, ?, ?, ?, ?)
                """,
                (
                    plot["id"],
                    event_time,
                    pest_type,
                    severity_label_to_score(severity),
                    image_url,
                    status_to_task_status(status),
                    crop_type,
                    "",  # pesticide_type 不自动填 action_type（如 spray），避免药剂列显示动作名
                    event_id,
                ),
            )

        if action_type.lower() != "none":
            conn.execute(
                """
                INSERT INTO operation_logs (
                    plot_id,
                    action_type,
                    amount,
                    unit,
                    reason,
                    result,
                    operator,
                    created_at,
                    source_event_id
                )
                VALUES (?, ?, NULL, NULL, ?, ?, ?, ?, ?)
                """,
                (
                    plot["id"],
                    action_type,
                    f"device-event {event_id}",
                    "done" if status == "completed" else "partial",
                    robot_id,
                    event_time,
                    event_id,
                ),
            )

    return {
        "ok": True,
        "duplicate": False,
        "id": cur.lastrowid,
        "event_id": event_id,
        "event_uuid": event_uuid,
        "created_at": created_at,
    }


@app.post("/api/device/images")
def upload_device_image(
    event_id: str = Form(...),
    block_id: str = Form(""),
    pest_type: str = Form(""),
    sim_date: str = Form(""),
    file: UploadFile = File(...),
    _: None = Depends(require_write_token),
) -> dict:
    safe_event_id = event_id.strip()
    if not safe_event_id:
        raise HTTPException(status_code=422, detail="event_id is required")

    safe_block_id = block_id.strip()
    raw_pest_type = pest_type.strip()
    safe_pest_type = MAIXCAM_PEST_TYPE_MAP.get(raw_pest_type.lower(), raw_pest_type)
    safe_sim_date = sim_date.strip()
    print(
        f"[DEVICE_IMAGE] request event_id={safe_event_id} "
        f"block={safe_block_id or '-'} pest={safe_pest_type or '-'}"
    )

    # ---- Hub 匹配逻辑：如果没传 block_id，从最近的 ESP32 NFC 事件自动匹配 ----
    if not safe_block_id:
        with _hub_lock:
            for rid, state in list(_hub_pending.items()):
                if time.time() - state.get("updated_at", 0) > 300:
                    _hub_pending.pop(rid, None)
                    continue
                if state.get("block"):
                    safe_block_id = f"A-{state['block']:02d}"
                    print(f"[DEVICE_IMAGE] resolved block from hub: {safe_block_id}")
                    break

    # ---- 内存态丢失时，回退到数据库中最近5分钟的NFC区块 ----
    if not safe_block_id:
        with get_conn() as conn:
            safe_block_id = get_recent_hub_block(conn, DEFAULT_HUB_ROBOT_ID)
        if safe_block_id:
            print(f"[DEVICE_IMAGE] resolved block from db: {safe_block_id}")

    if safe_block_id and safe_pest_type and safe_pest_type.lower() != "none":
        if not is_expected_block_fruit(safe_block_id, safe_pest_type):
            print(
                f"[DEVICE_IMAGE] ignored event_id={safe_event_id} "
                f"reason=unexpected_fruit_for_block block={safe_block_id} pest={safe_pest_type}"
            )
            return {
                "ok": True,
                "ignored": True,
                "accepted": False,
                "uploaded": False,
                "reason": "unexpected_fruit_for_block",
                "message": "fruit type does not match the expected fruit for this block",
                "block_id": to_block_id_display(normalize_block_code(safe_block_id)),
                "pest_type": safe_pest_type,
            }
        if not is_expected_block_pest(safe_block_id, safe_pest_type):
            print(
                f"[DEVICE_IMAGE] ignored event_id={safe_event_id} "
                f"reason=unexpected_pest_for_block block={safe_block_id} pest={safe_pest_type}"
            )
            return {
                "ok": True,
                "ignored": True,
                "accepted": False,
                "uploaded": False,
                "reason": "unexpected_pest_for_block",
                "message": "pest_type does not match the expected pest for this block",
                "block_id": to_block_id_display(normalize_block_code(safe_block_id)),
                "pest_type": safe_pest_type,
            }

    # ---- 如果 MaixCam2 传了 pest_type，也存入 hub 待处理 ----
    if safe_pest_type and safe_block_id:
        block_num = int(normalize_block_code(safe_block_id)[-2:])
        with _hub_lock:
            for rid, state in _hub_pending.items():
                if state.get("block") == block_num:
                    state["pest_type"] = safe_pest_type
                    state["updated_at"] = time.time()
                    break

    accepted_pest_type = safe_pest_type

    content_type = file.content_type or ""
    if not content_type.startswith("image/"):
        raise HTTPException(status_code=422, detail="file must be image type")

    ext = Path(file.filename or "frame.jpg").suffix.lower() or ".jpg"
    if ext not in {".jpg", ".jpeg", ".png", ".webp"}:
        ext = ".jpg"

    # ---- 支持历史日期：sim_date=2026-07-01 → 图片存到对应日期目录 ----
    if safe_sim_date and re.fullmatch(r"\d{4}-\d{2}-\d{2}", safe_sim_date):
        use_date = datetime.strptime(safe_sim_date, "%Y-%m-%d")
    else:
        use_date = datetime.now()

    day_dir = UPLOAD_DIR / use_date.strftime("%Y%m%d")
    day_dir.mkdir(parents=True, exist_ok=True)
    normalized_name = re.sub(r"[^A-Za-z0-9_-]", "_", safe_event_id)
    file_name = f"{normalized_name}_{use_date.strftime('%H%M%S')}{ext}"
    save_path = day_dir / file_name

    total_bytes = 0
    first_chunk = b""
    with save_path.open("wb") as buffer:
        while True:
            chunk = file.file.read(1024 * 1024)
            if not chunk:
                break
            if not first_chunk:
                first_chunk = chunk[:16]
            total_bytes += len(chunk)
            if total_bytes > MAX_UPLOAD_BYTES:
                buffer.close()
                save_path.unlink(missing_ok=True)
                raise HTTPException(status_code=413, detail="image file too large")
            buffer.write(chunk)

    if not first_chunk or not is_supported_image(first_chunk):
        save_path.unlink(missing_ok=True)
        raise HTTPException(status_code=422, detail="file content is not a supported image")

    image_url = f"/uploads/{day_dir.name}/{file_name}"
    created_at = use_date.strftime("%Y-%m-%dT%H:%M:%S")
    auto_created = False
    pest_record_status = "skipped_non_actionable"
    pest_record_id = None
    event_block_display = safe_block_id

    try:
        with get_conn() as conn:
            event = conn.execute(
                "SELECT id, plot_id, block_id FROM device_events WHERE event_id = ?",
                (safe_event_id,),
            ).fetchone()

            if not event:
                # ---- 自动创建事件：MaixCam2 可独立上传，不需等 ESP32 先上报 ----
                if not safe_block_id:
                    print(f"[DEVICE_IMAGE] rejected event_id={safe_event_id} reason=missing_block_id")
                    raise HTTPException(
                        status_code=422,
                        detail="event not found, please provide block_id (e.g. A-01) to auto-create",
                    )
                auto_created = True
                plot = get_plot_by_code(conn, safe_block_id)
                event_block_display = to_block_id_display(plot["code"])
                event_uuid = str(uuid4())
                today_str = use_date.strftime("%Y-%m-%d")
                # 统计当天同区块同病害已有图片数，≥3 张 → 严重，1-2 张 → 轻度
                existing_cnt = conn.execute(
                    """
                    SELECT COUNT(*) FROM pest_records
                    WHERE plot_id = ? AND pest_type = ? AND image_url IS NOT NULL AND image_url <> ''
                      AND date(detected_at) = ?
                    """,
                    (plot["id"], accepted_pest_type or "", today_str),
                ).fetchone()[0]
                sev_label = "严重" if existing_cnt >= 2 else "轻度"  # 第3张起=严重
                conn.execute(
                    """
                    INSERT INTO device_events (
                        event_uuid, event_id, robot_id, plot_id, block_id,
                        crop_type, pest_type, action_type, severity, status,
                        image_url, created_at
                    ) VALUES (?, ?, 'maixcam2', ?, ?, '', ?, 'none', ?, 'completed', ?, ?)
                    """,
                    (
                        event_uuid,
                        safe_event_id,
                        plot["id"],
                        event_block_display,
                        accepted_pest_type or "none",
                        sev_label,
                        image_url,
                        created_at,
                    ),
                )
                upsert_robot_status(conn, "maixcam2", plot["code"], "completed")
                print(
                    f"[DEVICE_IMAGE] accepted event_id={safe_event_id} auto_created=1 "
                    f"block={event_block_display} pest={accepted_pest_type or 'none'}"
                )
                pest_record_status, pest_record_id = ensure_actionable_pest_record(
                    conn,
                    plot_id=plot["id"],
                    event_id=safe_event_id,
                    block_id=event_block_display,
                    pest_type=accepted_pest_type,
                    detected_at=created_at,
                    image_url=image_url,
                    severity_score=0.9 if existing_cnt >= 2 else 0.5,
                )
            else:
                plot = conn.execute(
                    "SELECT id, code FROM plots WHERE id = ?",
                    (event["plot_id"],),
                ).fetchone()
                event_block_display = event["block_id"] or to_block_id_display(plot["code"])
                if safe_block_id:
                    request_block_display = to_block_id_display(normalize_block_code(safe_block_id))
                    if request_block_display != event_block_display:
                        print(
                            f"[DEVICE_IMAGE] block mismatch event_id={safe_event_id} "
                            f"event_block={event_block_display} request_block={request_block_display}"
                        )
                print(
                    f"[DEVICE_IMAGE] accepted event_id={safe_event_id} auto_created=0 "
                    f"block={event_block_display} pest={accepted_pest_type or 'none'}"
                )

            # ---- 事件已存在：正常挂载图片，同时更新 pest_type ----
            conn.execute(
                "UPDATE device_events SET image_url = ? WHERE event_id = ?",
                (image_url, safe_event_id),
            )
            if accepted_pest_type and accepted_pest_type.lower() != "none":
                conn.execute(
                    "UPDATE device_events SET pest_type = ? WHERE event_id = ?",
                    (accepted_pest_type, safe_event_id),
                )
            conn.execute(
                "UPDATE pest_records SET image_url = ? WHERE source_event_id = ?",
                (image_url, safe_event_id),
            )
            pest_record_status, pest_record_id = ensure_actionable_pest_record(
                conn,
                plot_id=plot["id"],
                event_id=safe_event_id,
                block_id=event_block_display,
                pest_type=accepted_pest_type,
                detected_at=created_at,
                image_url=image_url,
                severity_score=0.8,
            )
    except Exception:
        save_path.unlink(missing_ok=True)
        raise

    return {
        "ok": True,
        "accepted": True,
        "ignored": False,
        "uploaded": True,
        "event_id": safe_event_id,
        "image_url": image_url,
        "auto_created": auto_created,
        "block_id": event_block_display,
        "pest_type": accepted_pest_type,
        "pest_record_status": pest_record_status,
        "pest_record_id": pest_record_id,
    }




def _resolve_prescription(pest_cn: str, block_display: str = "") -> tuple[str, int]:
    pest_name = (pest_cn or "").strip()
    block_name = (block_display or "").strip()
    if not pest_name or pest_name.lower() in {"none", "正常", "未知", ""}:
        return ("芸苔素内酯（保健）", 5)
    if pest_name == "菜青虫":
        return ("阿维菌素", 1)
    if pest_name == "白粉病":
        return ("吡唑醚菌酯", 2)
    if pest_name == "褐斑病+菜青虫":
        return ("阿维菌素+吡唑醚菌酯", 6)
    if pest_name == "蜗牛":
        return ("杀螺胺乙醇胺盐悬", 4)
    if pest_name == "褐斑病":
        if block_name == "A-06":
            return ("", 0)  # 禁止施药，前端用BLOCK_RX锁定方案
        return ("吡唑醚菌酯", 2)
    return ("芸苔素内酯", 5)


@app.get("/api/device/command")
def get_device_command(robot_id: str = ""):
    """Return a fresh pest task associated with the robot's current NFC visit."""
    from uuid import uuid4

    request_id = str(uuid4())
    safe_robot_id = robot_id.strip()
    print(f"[DEVICE_COMMAND] poll robot_id={safe_robot_id or '-'} request_id={request_id}")
    if not safe_robot_id:
        print(f"[DEVICE_COMMAND] no command request_id={request_id} reason=missing_robot_id")
        return {"ok": True, "data": None, "request_id": request_id}
    if safe_robot_id != DEFAULT_HUB_ROBOT_ID:
        print(
            f"[DEVICE_COMMAND] no command request_id={request_id} "
            f"reason=unsupported_robot expected={DEFAULT_HUB_ROBOT_ID}"
        )
        return {"ok": True, "data": None, "request_id": request_id}

    with get_conn() as conn:
        active_block = get_recent_hub_block(conn, safe_robot_id)
        if not active_block:
            print(f"[DEVICE_COMMAND] no command request_id={request_id} reason=no_active_block")
            return {"ok": True, "data": None, "request_id": request_id}

        pest_rows = conn.execute(
            """
            SELECT pr.id, pr.plot_id, pr.pest_type, pr.detected_at, de.block_id
            FROM pest_records pr
            JOIN device_events de ON pr.source_event_id = de.event_id
            WHERE pr.task_status = ?
              AND pr.pest_type IN ('白粉病', '褐斑病', '菜青虫', '蜗牛', '褐斑病+菜青虫')
              AND datetime(pr.detected_at) >= datetime('now', 'localtime', '-5 minutes')
              AND EXISTS (
                  SELECT 1 FROM device_events nfc
                  WHERE nfc.robot_id = ?
                    AND (nfc.event_id LIKE 'nfc_detected-%' OR nfc.event_id LIKE 'nfc-detected-%')
                    AND nfc.status = 'running'
                    AND nfc.block_id = de.block_id
              )
            ORDER BY pr.id DESC
            """,
            (TASK_STATUS_PENDING, safe_robot_id),
        ).fetchall()

        pest = pest_rows[0] if pest_rows else None

        # 无效记录（无病害/正常检测）直接关闭，不给 ESP32
        if pest and (not pest["pest_type"] or pest["pest_type"].lower() in {"none", "正常", "未知", ""}):
            conn.execute(
                "UPDATE pest_records SET task_status = ? WHERE id = ?",
                (TASK_STATUS_COMPLETED, pest["id"]),
            )
            print(f"[DEVICE_COMMAND] no command request_id={request_id} reason=non_actionable_pest")
            return {"ok": True, "data": None, "request_id": request_id, "skipped": True}

        if not pest:
            print(
                f"[DEVICE_COMMAND] no command request_id={request_id} "
                f"reason=no_pending_task active_block={active_block}"
            )
            return {"ok": True, "data": None, "request_id": request_id}

        block_display = pest["block_id"] or to_block_id_display(
            conn.execute("SELECT code FROM plots WHERE id = ?", (pest["plot_id"],)).fetchone()["code"]
        )

        merged_pest_type = merge_block_pest_types(
            block_display,
            [row["pest_type"] for row in pest_rows if row["block_id"] == pest["block_id"]],
        ) or pest["pest_type"]

        # 立即标记执行中，避免重复下发
        for row in pest_rows:
            if row["block_id"] == pest["block_id"] and row["pest_type"] in {merged_pest_type, "褐斑病", "菜青虫"}:
                conn.execute(
                    "UPDATE pest_records SET task_status = ? WHERE id = ?",
                    (TASK_STATUS_RUNNING, row["id"]),
                )

        block_num = int(block_display.split("-")[-1]) if "-" in block_display else int(block_display)
        pesticide, pest_code = _resolve_prescription(merged_pest_type, block_display)
        payload = {
            "block": block_num,
            "pest_type": merged_pest_type,
            "pesticide": pesticide,
            "pest_code": pest_code,
            "detected_at": pest["detected_at"],
        }
        print(
            f"[DEVICE_COMMAND] return request_id={request_id} block={block_display} "
            f"pest={merged_pest_type} pesticide={pesticide or '-'}"
        )

        return {
            "ok": True,
            "request_id": request_id,
            "data": payload,
        }


@app.get("/api/farm/robot")
def get_farm_robot() -> dict:
    with get_conn() as conn:
        robot = conn.execute(
            """
            SELECT * FROM robot_status
            WHERE robot_id = ?
            ORDER BY updated_at DESC
            LIMIT 1
            """,
            (DEFAULT_HUB_ROBOT_ID,),
        ).fetchone()
        pending = conn.execute(
            """
            SELECT pr.pest_type, de.block_id
            FROM pest_records pr
            JOIN device_events de ON de.event_id = pr.source_event_id
            WHERE pr.task_status = ?
            ORDER BY pr.id DESC
            LIMIT 1
            """,
            (TASK_STATUS_PENDING,),
        ).fetchone()

    block_display = robot["location_plot_code"] if robot and robot["location_plot_code"] else ""
    block_num = int(normalize_block_code(block_display)[-2:]) if block_display else 0
    pending_block = pending["block_id"] if pending else ""
    robot_block_display = to_block_id_display(block_display) if block_display else ""
    pesticide = _resolve_prescription(pending["pest_type"], pending_block)[0] if pending and pending_block == robot_block_display else ""
    prescription = {
        "pesticide_a": pesticide,
        "pesticide_b": "",
        "ratio_a": 30 if pesticide else 0,
        "ratio_b": 0,
        "water": 70 if pesticide else 0,
        "ratio_label": "3:7" if pesticide else "",
        "severity": "轻度" if pesticide else "正常",
    }
    return {
        "ok": True,
        "robot_id": robot["robot_id"] if robot else DEFAULT_HUB_ROBOT_ID,
        "status": robot["status"] if robot else "idle",
        "block": block_num,
        "updated_at": robot["updated_at"] if robot else "",
        "prescription": prescription,
    }


@app.post("/api/sim/clear")
def clear_simulation_state(_: None = Depends(require_write_token)) -> dict:
    with _hub_lock:
        _hub_pending.clear()
    # 清理模拟 + 摄像头数据，恢复初始种子状态（与监控页初始值一一对应）
    with get_conn() as conn:
        conn.execute("DELETE FROM device_events WHERE robot_id = 'sim-robot-01'")
        conn.execute("DELETE FROM pest_records WHERE model_name = 'sim-robot-01'")
        conn.execute("DELETE FROM operation_logs WHERE operator = 'sim-robot-01'")
        conn.execute("DELETE FROM robot_status WHERE robot_id = 'sim-robot-01'")
        # 清除种子事件上残留的图片和病虫害（仅限模拟/种子数据）
        conn.execute(
            "UPDATE device_events SET image_url = NULL, pest_type = 'none' "
            "WHERE robot_id IN ('sim-robot-01', 'seed-demo')"
        )
        conn.execute(
            """
            UPDATE robot_status
            SET location_plot_code = NULL, status = 'idle', updated_at = ?
            WHERE robot_id = 'esp32-s3-hub-001'
            """,
            (now_iso(),),
        )
        # 重置所有 pest_records 任务状态
        conn.execute("UPDATE pest_records SET task_status = '已完成' WHERE id IN (SELECT id FROM pest_records)")
    return {"ok": True, "cleared": True}


@app.get("/api/farm/weather")
async def get_farm_weather():
    current_time = time.time()
    
    # 根据经纬度确定显示的位置，或者这里可以做反向地理编码。目前直接写死即可。
    location_name = "江苏省南京市" if FARM_LATITUDE == 32.06 else f"定位: {FARM_LATITUDE},{FARM_LONGITUDE}"

    async with _weather_lock:
        if _weather_cache["data"] and current_time - _weather_cache["timestamp"] < 900:
            return {"location": location_name, "forecast": _weather_cache["data"]}
        
    fallback_data = [
        { "weather": "晴转多云", "high": 29, "low": 20, "wind": "东北风 2级", "rain": "10%" },
        { "weather": "多云", "high": 27, "low": 19, "wind": "东风 3级", "rain": "20%" },
        { "weather": "小雨", "high": 25, "low": 18, "wind": "东南风 3级", "rain": "60%" },
        { "weather": "阵雨", "high": 24, "low": 17, "wind": "南风 2级", "rain": "70%" },
    ]
        
    try:
        url = (
            f"https://api.open-meteo.com/v1/forecast"
            f"?latitude={FARM_LATITUDE}&longitude={FARM_LONGITUDE}"
            f"&daily=weathercode,temperature_2m_max,temperature_2m_min,precipitation_probability_max,windspeed_10m_max"
            f"&timezone=Asia%2FShanghai"
        )
        async with httpx.AsyncClient() as client:
            resp = await client.get(url, timeout=5.0)
            resp.raise_for_status()
            data = resp.json()
            
            daily = data.get("daily", {})
            times = daily.get("time", [])
            codes = daily.get("weathercode", [])
            max_temps = daily.get("temperature_2m_max", [])
            min_temps = daily.get("temperature_2m_min", [])
            precips = daily.get("precipitation_probability_max", [])
            winds = daily.get("windspeed_10m_max", [])
            
            def get_weather_desc(code):
                if code == 0: return "晴天"
                if code in (1, 2, 3): return "多云"
                if code in (45, 48): return "雾天"
                if code in (51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82): return "雨天"
                if code in (71, 73, 75, 77, 85, 86): return "雪天"
                if code in (95, 96, 99): return "雷阵雨"
                return "未知"

            result = []
            for i in range(min(4, len(times))):
                result.append({
                    "weather": get_weather_desc(codes[i]),
                    "high": round(max_temps[i]),
                    "low": round(min_temps[i]),
                    "wind": f"东北风 {round(winds[i])}km/h",
                    "rain": f"{precips[i]}%"
                })
            
            if not result:
                return {"location": location_name, "forecast": fallback_data}
                
            async with _weather_lock:
                _weather_cache["data"] = result
                _weather_cache["timestamp"] = current_time
            return {"location": location_name, "forecast": result}
            
    except Exception as e:
        print(f"Fetch weather failed, using fallback: {e}")
        return {"location": location_name, "forecast": fallback_data}


@app.get("/api/farm/status")
def farm_status() -> dict:
    with get_conn() as conn:
        rows = conn.execute(
            """
            SELECT p.id, COALESCE(latest.severity, '正常') AS severity
            FROM plots p
            LEFT JOIN (
                SELECT e1.plot_id, e1.severity
                FROM device_events e1
                JOIN (
                    SELECT plot_id, MAX(id) AS max_id
                    FROM device_events
                    GROUP BY plot_id
                ) grouped ON grouped.max_id = e1.id
            ) latest ON latest.plot_id = p.id
            ORDER BY p.code
            """
        ).fetchall()

    normal_count = 0
    warning_count = 0
    danger_count = 0
    for row in rows:
        if row["severity"] == "严重":
            danger_count += 1
        elif row["severity"] == "轻度":
            warning_count += 1
        else:
            normal_count += 1

    return {
        "total": len(rows),
        "normal": normal_count,
        "warning": warning_count,
        "danger": danger_count,
        "updated_at": now_iso(),
    }


@app.get("/api/farm/plots")
def farm_plots() -> dict:
    with get_conn() as conn:
        rows = conn.execute(
            """
            SELECT
                p.id AS plot_id,
                p.code AS plot_code,
                p.name AS plot_name,
                COALESCE(
                    NULLIF(latest.crop_type, ''),
                    (
                        SELECT cc2.variety
                        FROM crop_cycles cc2
                        WHERE cc2.plot_id = p.id
                        ORDER BY cc2.id DESC
                        LIMIT 1
                    ),
                    '未知'
                ) AS crop_type,
                latest.temperature,
                latest.humidity,
                COALESCE(latest_pest_image.image_url, '') AS image_url,
                COALESCE(latest_pest_image.pest_type, '') AS pest_type,
                COALESCE(latest_pest_image.created_at, '') AS image_created_at,
                COALESCE(latest.status, 'idle') AS status,
                CASE
                    WHEN img_cnt.cnt >= 3 THEN '严重'
                    WHEN img_cnt.cnt >= 1 THEN '轻度'
                    ELSE '正常'
                END AS severity,
                CASE
                    WHEN p.code = 'A06' THEN '待复查'
                    WHEN pest.task_status IN ('待执行', '执行中') THEN
                        CASE p.code
                            WHEN 'A01' THEN '严重'
                            WHEN 'A04' THEN '轻度'
                            WHEN 'A02' THEN '轻度'
                            WHEN 'A03' THEN '轻度'
                            ELSE '正常'
                        END
                    WHEN img_cnt.cnt > 0 THEN
                        CASE p.code
                            WHEN 'A01' THEN '严重'
                            WHEN 'A05' THEN '正常'
                            ELSE '轻度'
                        END
                    ELSE
                        CASE p.code
                            WHEN 'A01' THEN '轻度异常'
                            WHEN 'A04' THEN '病害预警'
                            ELSE '正常'
                        END
                END AS governance,
                latest.created_at AS updated_at,
                cc.expected_harvest_at
            FROM plots p
            LEFT JOIN (
                SELECT e1.*
                FROM device_events e1
                JOIN (
                    SELECT plot_id, MAX(id) AS max_id
                    FROM device_events
                    GROUP BY plot_id
                ) grouped ON grouped.max_id = e1.id
                        ) latest ON latest.plot_id = p.id
                        LEFT JOIN (
                                SELECT e1.plot_id, e1.image_url, e1.pest_type, e1.created_at
                                FROM device_events e1
                                JOIN (
                                        SELECT plot_id, MAX(created_at) AS max_created
                                        FROM device_events
                                        WHERE image_url IS NOT NULL AND image_url <> ''
                                            AND pest_type IN ('白粉病', '褐斑病', '菜青虫', '蜗牛', '褐斑病+菜青虫')
                                        GROUP BY plot_id
                                ) grouped_image ON grouped_image.max_created = e1.created_at AND grouped_image.plot_id = e1.plot_id
                                WHERE e1.image_url IS NOT NULL AND e1.image_url <> ''
                        ) latest_pest_image ON latest_pest_image.plot_id = p.id
            LEFT JOIN crop_cycles cc ON cc.plot_id = p.id AND cc.status = 'growing'
            LEFT JOIN (
                SELECT plot_id, COUNT(*) AS cnt
                FROM device_events
                WHERE image_url IS NOT NULL AND image_url <> ''
                  AND pest_type NOT IN ('猕猴桃', '橘子', 'mihoutao', 'juzi')
                GROUP BY plot_id
            ) img_cnt ON img_cnt.plot_id = p.id
            LEFT JOIN (
                SELECT plot_id, task_status
                FROM pest_records
                WHERE pest_type IN ('白粉病', '褐斑病', '菜青虫', '蜗牛', '褐斑病+菜青虫')
                  AND id IN (
                      SELECT MAX(id)
                      FROM pest_records
                      WHERE pest_type IN ('白粉病', '褐斑病', '菜青虫', '蜗牛', '褐斑病+菜青虫')
                      GROUP BY plot_id
                  )
            ) pest ON pest.plot_id = p.id
            ORDER BY p.code
            """
        ).fetchall()

    items = []
    for idx, row in enumerate(rows, start=1):
        status = row["status"] if row["status"] in {"idle", "running", "completed", "error"} else "idle"
        block_display = to_block_id_display(row["plot_code"])
        img_url = row["image_url"] if "image_url" in row.keys() else ""
        pest_tp = row["pest_type"] if "pest_type" in row.keys() else ""
        img_at = row["image_created_at"] if "image_created_at" in row.keys() else ""
        # 无预期病虫害的区块（如A-05）不展示病虫害图片
        if block_display in BLOCK_EXPECTED_PESTS and not BLOCK_EXPECTED_PESTS[block_display]:
            img_url = ""
            pest_tp = ""
            img_at = ""
        items.append(
            {
                "seq": idx,
                "block_id": block_display,
                "plot_name": row["plot_name"],
                "crop_type": row["crop_type"],
                "expected_harvest_at": row["expected_harvest_at"] or "",
                "temperature": row["temperature"],
                "humidity": row["humidity"],
                "severity": row["severity"],
                "governance": row["governance"],
                "status": status,
                "status_label": status_to_display(status),
                "updated_at": row["updated_at"],
                "image_url": img_url,
                "pest_type": pest_tp,
                "image_created_at": img_at,
            }
        )
    return {"items": items}


@app.get("/api/farm/plots/{block_id}/detail")
def farm_plot_detail(block_id: str) -> dict:
    with get_conn() as conn:
        plot = get_plot_by_code(conn, block_id)
        latest = conn.execute(
            """
            SELECT event_id, block_id, crop_type, pest_type, action_type, severity, status, temperature, humidity, device_time, image_url, created_at
            FROM device_events
            WHERE plot_id = ?
            ORDER BY id DESC
            LIMIT 1
            """,
            (plot["id"],),
        ).fetchone()

        pests = conn.execute(
            """
            SELECT detected_at, pest_type, severity, COALESCE(task_status, '待执行') AS task_status,
                   COALESCE(image_url, '') AS image_url,
                   COALESCE(crop_category, '') AS crop_category,
                   COALESCE(pesticide_type, '') AS pesticide_type
            FROM pest_records
            WHERE plot_id = ?
            ORDER BY id DESC
            LIMIT 30
            """,
            (plot["id"],),
        ).fetchall()

        operations = conn.execute(
            """
            SELECT created_at, action_type, amount, unit, reason, result, operator
            FROM operation_logs
            WHERE plot_id = ?
            ORDER BY id DESC
            LIMIT 30
            """,
            (plot["id"],),
        ).fetchall()

        fruit_types = ("猕猴桃", "橘子", "mihoutao", "juzi")
        pest_images = conn.execute(
                        """
                        SELECT event_id, image_url, created_at, severity, pest_type
                        FROM device_events
                        WHERE plot_id = ?
                            AND image_url IS NOT NULL
                            AND image_url <> ''
                            AND pest_type NOT IN (?, ?, ?, ?)
                        ORDER BY created_at DESC
                        LIMIT 200
                        """,
                        (plot["id"], *fruit_types),
        ).fetchall()

        fruit_images = conn.execute(
                        """
                        SELECT event_id, image_url, created_at, severity, pest_type
                        FROM device_events
                        WHERE plot_id = ?
                            AND image_url IS NOT NULL
                            AND image_url <> ''
                            AND pest_type IN (?, ?, ?, ?)
                        ORDER BY created_at DESC
                        LIMIT 200
                        """,
                        (plot["id"], *fruit_types),
        ).fetchall()

        event_history = conn.execute(
            """
            SELECT event_id, pest_type, action_type, severity, status, temperature, humidity, created_at
            FROM device_events
            WHERE plot_id = ?
            ORDER BY id DESC
            LIMIT 40
            """,
            (plot["id"],),
        ).fetchall()

    latest_payload = None
    if latest:
        status = latest["status"] if latest["status"] in {"idle", "running", "completed", "error"} else "idle"
        latest_payload = {
            "event_id": latest["event_id"],
            "crop_type": latest["crop_type"],
            "pest_type": latest["pest_type"],
            "action_type": latest["action_type"],
            "pesticide": _resolve_prescription(latest["pest_type"], latest["block_id"])[0],
            "severity": latest["severity"],
            "status": status,
            "status_label": status_to_display(status),
            "temperature": latest["temperature"],
            "humidity": latest["humidity"],
            "device_time": latest["device_time"],
            "image_url": latest["image_url"],
            "updated_at": latest["created_at"],
        }

    return {
        "plot": {
            "code": to_block_id_display(plot["code"]),
            "name": plot["name"],
            "area_m2": plot["area_m2"],
        },
        "latest": latest_payload,
        "images": [dict(x) for x in pest_images],
        "fruit_images": [dict(x) for x in fruit_images],
        "pest_history": [
            {
                "detected_at": row["detected_at"],
                "pest_type": row["pest_type"],
                "severity": severity_score_to_label(row["severity"]),
                "task_status": row["task_status"],
                "crop_category": row["crop_category"],
                "pesticide_type": row["pesticide_type"],
                "image_url": row["image_url"],
            }
            for row in pests
        ],
        "operation_history": [dict(x) for x in operations],
        "event_history": [
            {
                **dict(row),
                "status_label": status_to_display(
                    row["status"] if row["status"] in {"idle", "running", "completed", "error"} else "idle"
                ),
            }
            for row in event_history
        ],
    }

@app.get("/api/farm/plots/{block_id}/prescription")
def get_plot_prescription(block_id: str):
    """
    Industry 4.0 Flexible Decision API
    - Evaluates pre-harvest intervals (PHI) safety locks
    - Prevents pesticide resistance by avoiding repeated types
    """
    with get_conn() as conn:
        normalized_code = normalize_block_code(block_id)
        cycle = conn.execute(
            """
            SELECT c.*, p.name as plot_name 
            FROM crop_cycles c
            JOIN plots p ON p.id = c.plot_id
            WHERE p.code = ?
            ORDER BY c.planted_at DESC LIMIT 1
            """,
            (normalized_code,)
        ).fetchone()

        if not cycle:
            raise HTTPException(status_code=404, detail="Crop cycle not found")

        expected_harvest_at = cycle["expected_harvest_at"] # YYYY-MM-DD
        try:
            harvest_date = datetime.strptime(expected_harvest_at or "", "%Y-%m-%d").date()
        except ValueError:
            harvest_date = date.today() # fallback if parsing fails

        today = date.today()
        days_to_harvest = (harvest_date - today).days

        # Get last pesticide type for resistance check
        last_pest_record = conn.execute(
            """
            SELECT pr.*, p.type as pesticide_type_name
            FROM pest_records pr
            JOIN plots pl ON pl.id = pr.plot_id
            LEFT JOIN pesticides p ON p.id = pr.pesticide_id
            WHERE pl.code = ?
            ORDER BY pr.detected_at DESC LIMIT 1
            """,
            (normalized_code,)
        ).fetchone()

        last_pesticide_type = last_pest_record["pesticide_type_name"] if last_pest_record else None

        available_pesticides = conn.execute("SELECT * FROM pesticides").fetchall()
        
        recommendations = []
        warnings = []
        
        if days_to_harvest < 0:
            warnings.append("已过预计采摘期，建议立刻采摘，禁止施药。")
        elif days_to_harvest <= 7:
            warnings.append(f"临近采收（{days_to_harvest}天），PHI高风险区，强烈建议物理/生物防治。")
        
        for pest in available_pesticides:
            reason = "符合安全标准"
            is_recommended = True
            is_locked = False
            tags = []
            
            # PHI check
            if days_to_harvest >= 0 and pest["phi_days"] > days_to_harvest:
                is_recommended = False
                is_locked = True
                reason = f"安全系统锁死：所需PHI({pest['phi_days']}天)超出采收余期({days_to_harvest}天)"
                tags.append("PHI超标禁止")
            
            # Resistance check
            elif is_recommended and last_pesticide_type and pest["type"] == last_pesticide_type:
                is_recommended = False
                reason = f"抗药性拦截：与上次使用的（{last_pesticide_type}）相同机理"
                tags.append("抗药性风险")
                
            if is_recommended:
                tags.append("推荐使用")
                
            recommendations.append({
                "pesticide_id": pest["id"],
                "name": pest["name"],
                "type": pest["type"],
                "base_dosage": pest["base_dosage"],
                "phi_days": pest["phi_days"],
                "is_recommended": is_recommended,
                "is_locked": is_locked,
                "reason": reason,
                "tags": tags
            })
            
        recommendations.sort(key=lambda x: (x["is_locked"], not x["is_recommended"]))

        timeline = [
            {"date": today.isoformat(), "event": "系统计算当前方案", "status": "current"},
            {"date": expected_harvest_at, "event": ("已超期" if days_to_harvest < 0 else f"预计采收"), "status": "future"}
        ]
        if last_pest_record:
            timeline.insert(0, {
                "date": last_pest_record["detected_at"][:10],
                "event": f"最近施药: {last_pesticide_type or '未知'}类",
                "status": "past"
            })

        return {
            "plot_id": block_id,
            "plot_name": cycle["plot_name"],
            "expected_harvest_at": expected_harvest_at,
            "days_to_harvest": days_to_harvest,
            "last_used_pesticide_type": last_pesticide_type,
            "warnings": warnings,
            "recommendations": recommendations,
            "timeline": timeline
        }


@app.delete("/api/farm/plots/{block_id}")
def clear_farm_plot_records(
    block_id: str,
    _: None = Depends(require_write_token),
) -> dict:
    with get_conn() as conn:
        plot = get_plot_by_code(conn, block_id)
        event_deleted = conn.execute(
            "DELETE FROM device_events WHERE plot_id = ?",
            (plot["id"],),
        ).rowcount
        pest_deleted = conn.execute(
            "DELETE FROM pest_records WHERE plot_id = ?",
            (plot["id"],),
        ).rowcount
        operation_deleted = conn.execute(
            "DELETE FROM operation_logs WHERE plot_id = ?",
            (plot["id"],),
        ).rowcount

    return {
        "ok": True,
        "block_id": to_block_id_display(plot["code"]),
        "deleted": {
            "events": event_deleted,
            "pest_records": pest_deleted,
            "operation_logs": operation_deleted,
        },
    }


class PatchEventPayload(BaseModel):
    created_at: Optional[str] = None      # 格式 "2026-06-28T15:30:00"
    pest_type: Optional[str] = None


class PatchHarvestPayload(BaseModel):
    expected_harvest_at: str


class AIAdvisePayload(BaseModel):
    question: str


@app.post("/api/ai/advise")
def ai_advise(payload: AIAdvisePayload) -> dict:
    question = payload.question.strip()
    if not question:
        raise HTTPException(status_code=422, detail="question is required")

    pest_keywords = {
        "菜青虫": "阿维菌素可用于菜青虫防治，注意按标签稀释并避开采收安全间隔。",
        "蜗牛": "蜗牛高发时优先清理潮湿隐蔽物，可配合四聚乙醛颗粒诱杀。",
        "白粉病": "白粉病属于真菌病害，可考虑吡唑醚菌酯等杀菌剂轮换使用。",
        "褐斑病": "褐斑病建议摘除重病叶，必要时用保护性杀菌剂并注意轮换机理。",
        "蚜虫": "蚜虫可用黄板监测，药剂上可考虑阿维菌素或吡虫啉类并保护天敌。",
    }
    matched = [tip for name, tip in pest_keywords.items() if name in question]
    if not matched:
        matched = ["请先确认病虫害类型、发生部位、严重程度和预计采收日期，再选择药剂。"]

    answer = "\n".join([
        "基于本地知识库的保守建议：",
        *[f"- {tip}" for tip in matched],
        "- 临近采收期必须核对 PHI，禁止随意混配未知药剂。",
        "- 若病斑扩散快或识别不确定，建议人工复查后再执行喷洒。",
    ])
    return {"ok": True, "answer": answer}


@app.patch("/api/farm/plots/{block_id}/harvest")
@app.put("/api/farm/plots/{block_id}/harvest")
def patch_plot_harvest(
    block_id: str,
    payload: PatchHarvestPayload,
    _: None = Depends(require_write_token),
) -> dict:
    """编辑区块的预计采收日期（PATCH 或 PUT 均可）"""
    with get_conn() as conn:
        plot = get_plot_by_code(conn, block_id)
        conn.execute(
            "UPDATE crop_cycles SET expected_harvest_at = ? WHERE plot_id = ? AND status = 'growing'",
            (payload.expected_harvest_at, plot["id"]),
        )
    return {"ok": True, "block_id": to_block_id_display(plot["code"]), "expected_harvest_at": payload.expected_harvest_at}


@app.patch("/api/device/events/{event_id}")
def patch_device_event(
    event_id: str,
    payload: PatchEventPayload,
    _: None = Depends(require_write_token),
) -> dict:
    """编辑单条事件：移动日期、修正图片类别等"""
    with get_conn() as conn:
        event = conn.execute(
            "SELECT id, image_url, created_at, block_id, pest_type FROM device_events WHERE event_id = ?",
            (event_id,),
        ).fetchone()
        if not event:
            raise HTTPException(status_code=404, detail=f"event not found: {event_id}")

        new_pest_type = None
        new_created_at = payload.created_at
        if new_created_at:
            # 校验日期格式
            try:
                datetime.fromisoformat(new_created_at)
            except ValueError:
                raise HTTPException(status_code=422, detail="created_at format invalid, use ISO like 2026-06-28T15:30:00")
            conn.execute(
                "UPDATE device_events SET created_at = ? WHERE event_id = ?",
                (new_created_at, event_id),
            )
            conn.execute(
                "UPDATE pest_records SET detected_at = ? WHERE source_event_id = ?",
                (new_created_at, event_id),
            )

        if payload.pest_type is not None:
            new_pest_type = payload.pest_type.strip()
            if not new_pest_type:
                raise HTTPException(status_code=422, detail="pest_type is required")
            normalized_pest_type = MAIXCAM_PEST_TYPE_MAP.get(new_pest_type.lower(), new_pest_type)
            if normalized_pest_type in {"none", "未分类"}:
                normalized_pest_type = "未分类"

            conn.execute(
                "UPDATE device_events SET pest_type = ? WHERE event_id = ?",
                (normalized_pest_type, event_id),
            )
            conn.execute(
                "UPDATE pest_records SET pest_type = ? WHERE source_event_id = ?",
                (normalized_pest_type, event_id),
            )

        updated = conn.execute(
            "SELECT created_at, pest_type FROM device_events WHERE event_id = ?", (event_id,)
        ).fetchone()

    return {
        "ok": True,
        "event_id": event_id,
        "created_at": updated["created_at"],
        "pest_type": updated["pest_type"],
    }


@app.delete("/api/device/events/{event_id}")
def delete_device_event(
    event_id: str,
    _: None = Depends(require_write_token),
) -> dict:
    """删除单条事件及其图片文件"""
    with get_conn() as conn:
        event = conn.execute(
            "SELECT id, image_url FROM device_events WHERE event_id = ?",
            (event_id,),
        ).fetchone()
        if not event:
            raise HTTPException(status_code=404, detail=f"event not found: {event_id}")

        # 删图片文件
        if event["image_url"]:
            fp = Path(str(event["image_url"]).lstrip("/"))
            if fp.exists():
                fp.unlink()

        conn.execute("DELETE FROM device_events WHERE event_id = ?", (event_id,))
        conn.execute("DELETE FROM pest_records WHERE source_event_id = ?", (event_id,))

    return {"ok": True, "event_id": event_id}


@app.post("/api/nfc/read")
def nfc_read(payload: NfcReadRequest) -> dict:
    with get_conn() as conn:
        plot = get_plot_by_uid(conn, payload.nfc_uid)
        crop = conn.execute(
            """
            SELECT *
            FROM crop_cycles
            WHERE plot_id = ?
            ORDER BY id DESC
            LIMIT 1
            """,
            (plot["id"],),
        ).fetchone()
        pests = conn.execute(
            """
            SELECT detected_at, pest_type, severity, handled
            FROM pest_records
            WHERE plot_id = ?
            ORDER BY id DESC
            LIMIT 5
            """,
            (plot["id"],),
        ).fetchall()

    return {
        "plot": dict(plot),
        "crop_cycle": dict(crop) if crop else None,
        "recent_pest_history": [dict(x) for x in pests],
    }


@app.post("/api/detection/submit")
def detection_submit(
    payload: DetectionSubmitRequest,
    _: None = Depends(require_write_token),
) -> dict:
    with get_conn() as conn:
        plot = get_plot_by_uid(conn, payload.nfc_uid)
        detected_at = now_iso()
        cur = conn.execute(
            """
            INSERT INTO pest_records (plot_id, detected_at, pest_type, severity, model_name, image_url, task_status)
            VALUES (?, ?, ?, ?, ?, ?, '待执行')
            """,
            (
                plot["id"],
                detected_at,
                payload.pest_type,
                payload.severity,
                payload.model_name,
                payload.image_url,
            ),
        )
    return {"ok": True, "record_id": cur.lastrowid, "detected_at": detected_at}


@app.get("/api/detections/recent")
def list_recent_detections(limit: int = 20) -> dict:
    safe_limit = max(1, min(limit, 200))
    with get_conn() as conn:
        rows = conn.execute(
            """
            SELECT
                pr.id,
                p.code AS block_id,
                pr.plot_id,
                pr.pest_type,
                pr.severity,
                pr.detected_at AS handled_at,
                COALESCE(pr.task_status, '待执行') AS status,
                COALESCE(
                    NULLIF(pr.crop_category, ''),
                    (SELECT cc.variety FROM crop_cycles cc WHERE cc.plot_id = pr.plot_id ORDER BY cc.id DESC LIMIT 1),
                    '未知'
                ) AS crop_category,
                COALESCE(NULLIF(pr.pesticide_type, ''), '未施药') AS pesticide_type
            FROM pest_records pr
            JOIN plots p ON p.id = pr.plot_id
            WHERE pr.id IN (
                SELECT MAX(id) FROM pest_records
                WHERE pest_type IN ('白粉病', '褐斑病', '菜青虫', '蜗牛', '褐斑病+菜青虫')
                GROUP BY plot_id
            )
            ORDER BY pr.id DESC
            LIMIT ?
            """,
            (safe_limit,),
        ).fetchall()

    # 为 A-03 检查是否有第二类病虫害，合并显示
    merged_items = []
    for row in rows:
        block_raw = row["block_id"]
        block_display = to_block_id_display(block_raw)
        pest_type = row["pest_type"]
        pesticide = row["pesticide_type"]

        # A-03 合并褐斑病+菜青虫
        if block_display == "A-03" and pest_type in ("褐斑病", "菜青虫"):
            other = conn.execute(
                """SELECT pest_type FROM pest_records
                   WHERE plot_id = ? AND pest_type IN ('褐斑病','菜青虫')
                     AND pest_type != ?
                   ORDER BY id DESC LIMIT 1""",
                (row["plot_id"], pest_type),
            ).fetchone()
            if other:
                pest_type = "褐斑病+菜青虫"
                pesticide = "阿维菌素+吡唑醚菌酯"

        merged_items.append({
            "id": row["id"],
            "block_id": block_display,
            "pest_type": pest_type,
            "severity": severity_score_to_label(row["severity"]),
            "handled_at": row["handled_at"],
            "status": row["status"],
            "crop_category": row["crop_category"],
            "pesticide_type": pesticide,
        })

    return {"items": merged_items}


@app.post("/api/detections")
def create_detection(
    payload: DetectionCrudPayload,
    _: None = Depends(require_write_token),
) -> dict:
    with get_conn() as conn:
        plot = get_plot_by_code(conn, payload.block_id.strip())
        cur = conn.execute(
            """
            INSERT INTO pest_records (
                plot_id, detected_at, pest_type, severity, task_status, model_name, crop_category, pesticide_type
            )
            VALUES (?, ?, ?, ?, ?, 'manual-dashboard', ?, ?)
            """,
            (
                plot["id"],
                payload.handled_at.strip(),
                payload.pest_type.strip(),
                severity_label_to_score(payload.severity),
                payload.status,
                (payload.crop_category or "").strip(),
                (payload.pesticide_type or "").strip(),
            ),
        )
    return {"ok": True, "id": cur.lastrowid}


@app.put("/api/detections/{record_id}")
def update_detection(
    record_id: int,
    payload: DetectionCrudPayload,
    _: None = Depends(require_write_token),
) -> dict:
    with get_conn() as conn:
        existing = conn.execute(
            "SELECT id FROM pest_records WHERE id = ?",
            (record_id,),
        ).fetchone()
        if not existing:
            raise HTTPException(status_code=404, detail=f"detection not found: {record_id}")
        plot = get_plot_by_code(conn, payload.block_id.strip())
        conn.execute(
            """
            UPDATE pest_records
            SET plot_id = ?, detected_at = ?, pest_type = ?, severity = ?, task_status = ?, crop_category = ?, pesticide_type = ?
            WHERE id = ?
            """,
            (
                plot["id"],
                payload.handled_at.strip(),
                payload.pest_type.strip(),
                severity_label_to_score(payload.severity),
                payload.status,
                (payload.crop_category or "").strip(),
                (payload.pesticide_type or "").strip(),
                record_id,
            ),
        )
    return {"ok": True}


@app.delete("/api/detections/{record_id}")
def delete_detection(
    record_id: int,
    _: None = Depends(require_write_token),
) -> dict:
    with get_conn() as conn:
        cur = conn.execute("DELETE FROM pest_records WHERE id = ?", (record_id,))
        if cur.rowcount == 0:
            raise HTTPException(status_code=404, detail=f"detection not found: {record_id}")
    return {"ok": True}


@app.post("/api/command/dispatch")
def command_dispatch(
    payload: CommandRequest,
    _: None = Depends(require_write_token),
) -> dict:
    with get_conn() as conn:
        plot = get_plot_by_uid(conn, payload.nfc_uid)
    return {
        "ok": True,
        "command": {
            "plot_code": plot["code"],
            "action_type": payload.action_type,
            "amount": payload.amount,
            "unit": payload.unit,
            "reason": payload.reason,
            "dispatched_at": now_iso(),
        },
    }


@app.post("/api/nfc/writeback")
def nfc_writeback(
    payload: OperationWritebackRequest,
    _: None = Depends(require_write_token),
) -> dict:
    with get_conn() as conn:
        plot = get_plot_by_uid(conn, payload.nfc_uid)
        created_at = now_iso()
        cur = conn.execute(
            """
            INSERT INTO operation_logs (
                plot_id, action_type, amount, unit, reason, result, operator, created_at
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                plot["id"],
                payload.action_type,
                payload.amount,
                payload.unit,
                payload.reason,
                payload.result,
                payload.operator,
                created_at,
            ),
        )
    return {"ok": True, "operation_id": cur.lastrowid, "created_at": created_at}


@app.get("/api/plots/{plot_code}/history")
def plot_history(plot_code: str) -> dict:
    normalized_code = normalize_block_code(plot_code)
    with get_conn() as conn:
        plot = conn.execute(
            "SELECT * FROM plots WHERE code = ?",
            (normalized_code,),
        ).fetchone()
        if not plot:
            raise HTTPException(
                status_code=404,
                detail=f"plot not found: {to_block_id_display(normalized_code)}",
            )

        crop = conn.execute(
            """
            SELECT *
            FROM crop_cycles
            WHERE plot_id = ?
            ORDER BY id DESC
            LIMIT 1
            """,
            (plot["id"],),
        ).fetchone()
        pest_list = conn.execute(
            """
            SELECT detected_at, pest_type, severity, handled
            FROM pest_records
            WHERE plot_id = ?
            ORDER BY id DESC
            LIMIT 20
            """,
            (plot["id"],),
        ).fetchall()
        operations = conn.execute(
            """
            SELECT created_at, action_type, amount, unit, reason, result, operator
            FROM operation_logs
            WHERE plot_id = ?
            ORDER BY id DESC
            LIMIT 20
            """,
            (plot["id"],),
        ).fetchall()

    return {
        "plot": dict(plot),
        "current_crop_cycle": dict(crop) if crop else None,
        "pest_history": [dict(x) for x in pest_list],
        "operation_history": [dict(x) for x in operations],
    }


@app.get("/api/dashboard/overview")
def dashboard_overview() -> dict:
    with get_conn() as conn:
        plots = conn.execute(
            "SELECT id, code, name, area_m2 FROM plots ORDER BY code"
        ).fetchall()
        plot_count = conn.execute("SELECT COUNT(*) AS c FROM plots").fetchone()["c"]
        unhandled_pest = conn.execute(
            "SELECT COUNT(*) AS c FROM pest_records WHERE handled = 0"
        ).fetchone()["c"]
        op_count = conn.execute(
            "SELECT COUNT(*) AS c FROM operation_logs"
        ).fetchone()["c"]
        robot = conn.execute(
            """
            SELECT robot_id, battery_level, pesticide_level, fertilizer_level, location_plot_code, status, updated_at
            FROM robot_status
            ORDER BY id DESC
            LIMIT 1
            """
        ).fetchone()
        inventory = conn.execute(
            "SELECT item_type, item_name, stock, unit, low_threshold FROM inventory ORDER BY item_type, item_name"
        ).fetchall()

    return {
        "summary": {
            "plot_count": plot_count,
            "unhandled_pest_records": unhandled_pest,
            "operation_total": op_count,
            "updated_at": now_iso(),
        },
        "plots": [dict(p) for p in plots],
        "robot": dict(robot) if robot else None,
        "inventory": [dict(i) for i in inventory],
    }
