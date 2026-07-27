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
    task_status TEXT NOT NULL DEFAULT '待执行',
    crop_category TEXT NOT NULL DEFAULT '',
    pesticide_type TEXT NOT NULL DEFAULT '',
    source_event_id TEXT,
    pesticide_id INTEGER,
    action_detail TEXT,
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
    source_event_id TEXT,
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
