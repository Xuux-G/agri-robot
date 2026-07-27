# config.example.py — copy to config.py and fill in your values
# --- WiFi ---
WIFI_SSID = "YOUR_WIFI_SSID"
WIFI_PASS = "YOUR_WIFI_PASSWORD"
# --- Base Station Server ---
BASE_STATION_HOST = "192.168.x.x"
BASE_STATION_PORT = 8000
BASE_STATION_URL = f"http://{BASE_STATION_HOST}:{BASE_STATION_PORT}"
# --- Device Identifier ---
DEVICE_ID = "esp32-s3-hub-001"
# --- UART1 (GPIO15/16) — CI1302 Voice Module ---
VOICE_UART_ID = 1
VOICE_TX = 15
VOICE_RX = 16
# --- UART2 (GPIO17/18) — STM32 ---
STM32_UART_ID = 2
STM32_TX = 17
STM32_RX = 18
STM32_BAUD = 9600
# --- GPIO48 — WS2812B RGB ---
RGB_PIN = 48
# --- Task Parameters ---
POLL_INTERVAL_MS = 500
LOOP_SLEEP_MS = 50
