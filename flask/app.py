"""
Flask backend — Security Camera Hub

Responsibilities:
  - Device registry  (Arduino UNO R4, ESP32-CAM)
  - Receives push ALERTS from Arduino (motion / gas)
  - Runs OpenCV + MediaPipe person detection on motion alerts
  - Sends push notifications via Expo Push API
  - Relays servo commands from the app to the Arduino
  - Proxies the ESP32-CAM MJPEG stream and single-frame snapshots
  - Serves a web dashboard
"""

import os
import time
import threading
import requests
from collections import deque

from flask import Flask, request, jsonify, Response, stream_with_context
from dotenv import load_dotenv

load_dotenv()

# ── config ────────────────────────────────────────────────────────────
HOST            = os.getenv("FLASK_HOST",     "0.0.0.0")
PORT            = int(os.getenv("FLASK_PORT", "5000"))
SCRAPE_INTERVAL = int(os.getenv("SCRAPE_INTERVAL", "2"))
DEVICE_TIMEOUT  = int(os.getenv("DEVICE_TIMEOUT",  "60"))

app = Flask(__name__)

# ── shared state ──────────────────────────────────────────────────────
devices          = {}          # ip  → {name, type, last_seen}
latest_data      = {}          # ip  → sensor dict
detection_enabled = False      # controlled by app toggle
pending_cmds    = {}          # device_name → {"servo": angle | None}
push_tokens     = []          # Expo push token strings
alert_history   = deque(maxlen=50)   # last 50 alerts

_lock = threading.Lock()


# ══════════════════════════════════════════════════════════════════════
#  HELPERS
# ══════════════════════════════════════════════════════════════════════

def get_camera_ip():
    """Return the IP of the first registered camera device, or None."""
    with _lock:
        for ip, dev in devices.items():
            if dev.get("type") == "camera":
                return ip
    return None


def send_push_notification(title: str, body: str, data: dict = None):
    """Send an Expo push notification to all registered tokens."""
    if not push_tokens:
        return
    messages = [
        {
            "to":    token,
            "sound": "default",
            "title": title,
            "body":  body,
            "data":  data or {},
        }
        for token in push_tokens
    ]
    try:
        requests.post(
            "https://exp.host/--/api/v2/push/send",
            json=messages,
            timeout=10,
        )
    except Exception as e:
        print(f"[PUSH] failed: {e}")


def _detect_and_notify(alert_type: str, extra: dict):
    """
    Background worker: grab a camera snapshot, run person detection,
    then fire the appropriate push notification.
    - Motion alerts only run when detection_enabled is True.
    - Gas alerts always notify regardless of the toggle.
    """
    global detection_enabled

    try:
        if alert_type == "motion":
            if not detection_enabled:
                return  # toggle is off — ignore motion alerts

            from person_detector import detect_person

            cam_ip = get_camera_ip()
            if not cam_ip:
                print("[DETECT] no camera available for person detection")
                return

            r = requests.get(f"http://{cam_ip}/capture", timeout=5)
            if r.status_code != 200:
                print("[DETECT] snapshot failed")
                return

            is_person = detect_person(r.content)
            if is_person:
                entry = {
                    "type":      "person",
                    "device":    extra.get("device", "unknown"),
                    "timestamp": time.time(),
                }
                with _lock:
                    alert_history.appendleft(entry)
                send_push_notification(
                    "Person Detected",
                    "Movement detected — a person is in the frame.",
                    {"type": "person"},
                )
            else:
                print("[DETECT] motion detected but no person found — no alert sent")

        elif alert_type == "gas":
            gas = extra.get("gas", "Unknown")
            ppm = extra.get("ppm", "?")
            send_push_notification(
                "⚠️ Gas Detected",
                f"{gas} at {ppm} ppm — check the area immediately!",
                {"type": "gas", "gas": gas, "ppm": ppm},
            )

    except Exception as e:
        print(f"[DETECT] error: {e}")


# ══════════════════════════════════════════════════════════════════════
#  DEVICE REGISTRATION
# ══════════════════════════════════════════════════════════════════════

@app.route("/register", methods=["POST"])
def register():
    data = request.json or {}
    ip   = data.get("ip")
    name = data.get("name", "device")
    kind = data.get("type", "sensor")

    if not ip:
        return {"error": "missing ip"}, 400

    with _lock:
        devices[ip] = {"name": name, "type": kind, "last_seen": time.time()}
        if name not in pending_cmds:
            pending_cmds[name] = {"servo": None}

    print(f"[REGISTER] {name} ({kind}) @ {ip}")
    return {"status": "registered"}


# ══════════════════════════════════════════════════════════════════════
#  ALERT ENDPOINT  (called by Arduino)
# ══════════════════════════════════════════════════════════════════════

@app.route("/alert", methods=["POST"])
def alert():
    data        = request.json or {}
    alert_type  = data.get("type")   # "motion" or "gas"
    device      = data.get("device", "unknown")

    print(f"[ALERT] {alert_type} from {device}")

    if alert_type == "gas":
        # Gas alerts always go to history and always notify
        entry = {**data, "timestamp": time.time()}
        with _lock:
            alert_history.appendleft(entry)
        threading.Thread(
            target=_detect_and_notify,
            args=("gas", {"gas": data.get("gas", "Unknown"), "ppm": data.get("ppm", "?")}),
            daemon=True,
        ).start()

    elif alert_type == "motion":
        # Motion only processed when detection switch is ON.
        # History and notification are added only after person is confirmed.
        if detection_enabled:
            threading.Thread(
                target=_detect_and_notify,
                args=("motion", {"device": device}),
                daemon=True,
            ).start()

    return {"status": "ok"}


# ══════════════════════════════════════════════════════════════════════
#  SERVO COMMAND  (POST from app, GET from Arduino)
# ══════════════════════════════════════════════════════════════════════

@app.route("/servo", methods=["POST"])
def set_servo():
    data  = request.json or {}
    angle = data.get("angle")
    if angle is None or not (0 <= int(angle) <= 180):
        return {"error": "angle must be 0-180"}, 400

    with _lock:
        if "ESP32_S3" not in pending_cmds:
            pending_cmds["ESP32_S3"] = {}
        pending_cmds["ESP32_S3"]["servo"] = int(angle)

    print(f"[SERVO] queued angle={angle}")
    return {"status": "queued", "angle": angle}


@app.route("/command/<device_name>", methods=["GET"])
def get_command(device_name):
    """Arduino polls this; command is cleared after one read."""
    with _lock:
        cmd = pending_cmds.get(device_name, {})
        servo = cmd.get("servo", None)
        # Clear after delivering
        if device_name in pending_cmds:
            pending_cmds[device_name]["servo"] = None
    return jsonify({"servo": servo})


# ══════════════════════════════════════════════════════════════════════
#  PERSON DETECTION TOGGLE  (called by app)
# ══════════════════════════════════════════════════════════════════════

@app.route("/set-detection", methods=["POST"])
def set_detection():
    global detection_enabled
    data = request.json or {}
    detection_enabled = bool(data.get("enabled", False))
    print(f"[DETECTION] {'enabled' if detection_enabled else 'disabled'}")
    return {"status": "ok", "enabled": detection_enabled}


@app.route("/detection-status")
def detection_status():
    return jsonify({"enabled": detection_enabled})


# ══════════════════════════════════════════════════════════════════════
#  EXPO PUSH TOKEN REGISTRATION  (called by app)
# ══════════════════════════════════════════════════════════════════════

@app.route("/register-push-token", methods=["POST"])
def register_push_token():
    data  = request.json or {}
    token = data.get("token", "").strip()
    if not token:
        return {"error": "missing token"}, 400
    if token not in push_tokens:
        push_tokens.append(token)
        print(f"[PUSH] registered token: {token[:30]}...")
    return {"status": "ok"}


# ══════════════════════════════════════════════════════════════════════
#  CAMERA PROXY
# ══════════════════════════════════════════════════════════════════════

@app.route("/camera/stream")
def camera_stream():
    cam_ip = get_camera_ip()
    if not cam_ip:
        return "Camera not connected", 503

    def generate():
        with requests.get(f"http://{cam_ip}/stream", stream=True, timeout=30) as r:
            for chunk in r.iter_content(chunk_size=4096):
                yield chunk

    return Response(
        stream_with_context(generate()),
        content_type="multipart/x-mixed-replace;boundary=frame",
    )


@app.route("/camera/snapshot")
def camera_snapshot():
    cam_ip = get_camera_ip()
    if not cam_ip:
        return "Camera not connected", 503
    try:
        r = requests.get(f"http://{cam_ip}/capture", timeout=5)
        return Response(r.content, content_type="image/jpeg")
    except Exception as e:
        return str(e), 503


@app.route("/camera/url")
def camera_url():
    cam_ip = get_camera_ip()
    if not cam_ip:
        return jsonify({"stream": None, "snapshot": None})
    return jsonify({
        "stream":   f"http://{cam_ip}:81/stream",
        "snapshot": f"http://{cam_ip}/capture",
    })


# ══════════════════════════════════════════════════════════════════════
#  API ENDPOINTS
# ══════════════════════════════════════════════════════════════════════

@app.route("/api/devices")
def api_devices():
    return jsonify(devices)


@app.route("/api/data")
def api_data():
    return jsonify(latest_data)


@app.route("/api/alerts")
def api_alerts():
    return jsonify(list(alert_history))


# ══════════════════════════════════════════════════════════════════════
#  BACKGROUND SCRAPER  (polls /data on sensor devices)
# ══════════════════════════════════════════════════════════════════════

def scrape_device(ip):
    """Scrape one sensor device and return its data dict, or None on failure."""
    try:
        r = requests.get(f"http://{ip}/data", timeout=5)
        if r.status_code == 200:
            return r.json()
    except Exception as e:
        print(f"[SCRAPER] {ip} unreachable: {e}")
    return None


def scraper_loop():
    time.sleep(3)
    while True:
        now = time.time()
        with _lock:
            ips = list(devices.items())

        for ip, dev in ips:
            if dev.get("type") == "camera":
                continue
            data = scrape_device(ip)
            if data is not None:
                with _lock:
                    latest_data[ip] = data
                    devices[ip]["last_seen"] = now

        # Evict timed-out devices
        with _lock:
            for ip in list(devices.keys()):
                if now - devices[ip]["last_seen"] > DEVICE_TIMEOUT:
                    print(f"[EVICT] {ip}")
                    devices.pop(ip, None)
                    latest_data.pop(ip, None)

        time.sleep(SCRAPE_INTERVAL)


threading.Thread(target=scraper_loop, daemon=True).start()


@app.route("/scrape")
def scrape_now():
    now = time.time()
    with _lock:
        ips = list(devices.items())
    for ip, dev in ips:
        if dev.get("type") == "camera":
            continue
        data = scrape_device(ip)
        if data is not None:
            with _lock:
                latest_data[ip] = data
                devices[ip]["last_seen"] = now
    return jsonify(latest_data)


# ══════════════════════════════════════════════════════════════════════
#  DASHBOARD
# ══════════════════════════════════════════════════════════════════════

@app.route("/")
def index():
    return """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Security Camera Dashboard</title>
  <style>
    body { font-family: monospace; background:#111; color:#eee; padding:20px; }
    h1   { color:#0f0; }
    h2   { color:#0af; margin-top:30px; }
    pre  { background:#222; padding:12px; border-radius:6px; overflow:auto; }
    img  { max-width:640px; border:2px solid #0af; border-radius:6px; display:block; margin-top:10px; }
    .row { display:flex; gap:20px; flex-wrap:wrap; }
    .card{ background:#1a1a1a; padding:12px; border-radius:8px; min-width:220px; }
  </style>
</head>
<body>
  <h1>Security Camera Dashboard</h1>

  <h2>Camera Feed</h2>
  <img id="cam" src="" alt="No camera connected">

  <div class="row" style="margin-top:20px">
    <div class="card">
      <h2 style="margin-top:0">Devices</h2>
      <pre id="devices">Loading...</pre>
    </div>
    <div class="card">
      <h2 style="margin-top:0">Sensor Data</h2>
      <pre id="data">Loading...</pre>
    </div>
    <div class="card">
      <h2 style="margin-top:0">Recent Alerts</h2>
      <pre id="alerts">Loading...</pre>
    </div>
  </div>

<script>
async function refresh() {
  try {
    const [devRes, dataRes, alertRes, camRes] = await Promise.all([
      fetch('/api/devices'), fetch('/api/data'),
      fetch('/api/alerts'),  fetch('/camera/url'),
    ]);
    document.getElementById('devices').textContent =
      JSON.stringify(await devRes.json(), null, 2);
    document.getElementById('data').textContent =
      JSON.stringify(await dataRes.json(), null, 2);

    const alerts = await alertRes.json();
    document.getElementById('alerts').textContent =
      JSON.stringify(alerts.slice(0,10), null, 2);

    const cam = await camRes.json();
    const img = document.getElementById('cam');
    if (cam.snapshot) {
      img.src = cam.snapshot + '?t=' + Date.now();
    }
  } catch(e) { console.error(e); }
}
setInterval(refresh, 1500);
refresh();
</script>
</body>
</html>"""


# ══════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    print(f"Starting on {HOST}:{PORT}")
    app.run(host=HOST, port=PORT, debug=False, threaded=True)
