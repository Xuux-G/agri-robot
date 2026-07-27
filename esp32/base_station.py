try:
    import urequests
except ImportError:
    urequests = None
try:
    import ujson as json
except ImportError:
    import json
import time
from config import BASE_STATION_URL, DEVICE_ID


def report_nfc(block_num):
    if urequests is None:
        print("[BS] urequests not available")
        return False
    try:
        url = f"{BASE_STATION_URL}/api/device/events"
        event_id = f"nfc-block-{block_num}-{int(time.time())}"
        payload = {
            "event_id": event_id,
            "robot_id": DEVICE_ID,
            "block_id": f"A-{block_num:02d}",
            "event_type": "nfc_detected",
            "status": "completed",
        }
        print(f"[BS] POST nfc block {block_num}")
        resp = urequests.post(url, json=payload)
        body = resp.text
        resp.close()
        return resp.status_code == 200
    except Exception as e:
        print(f"[BS] report_nfc failed: {e}")
        return False


def poll_prescription():
    if urequests is None:
        return None
    try:
        url = f"{BASE_STATION_URL}/api/device/command"
        resp = urequests.get(url)
        if resp.status_code == 200:
            data = resp.json()
            resp.close()
            return data if data else None
        resp.close()
        return None
    except Exception as e:
        return None
