import os
import time
import threading
import requests
import json

from flask import Flask, request, jsonify
from dotenv import load_dotenv

# ---------------- ENV ----------------
load_dotenv()

HOST = os.getenv("FLASK_HOST", "0.0.0.0")
PORT = int(os.getenv("FLASK_PORT", 5000))
SCRAPE_INTERVAL = int(os.getenv("SCRAPE_INTERVAL", 2))
DEVICE_TIMEOUT = int(os.getenv("DEVICE_TIMEOUT", 10))

# ---------------- APP ----------------
app = Flask(__name__)

devices = {}       # Registered devices
latest_data = {}   # Latest scraped sensor data

# ---------------- REGISTER DEVICE ----------------
@app.route("/register", methods=["POST"])
def register():
    data = request.json
    ip = data.get("ip")
    name = data.get("name", "arduino")

    if not ip:
        return {"error": "missing ip"}, 400

    devices[ip] = {
        "name": name,
        "last_seen": time.time()
    }

    print(f"[REGISTER] {name} @ {ip}")
    return {"status": "registered"}


# ---------------- SCRAPER ----------------
def scraper_loop():
    global latest_data
    time.sleep(2)  # wait 2 seconds for devices to register
    while True:
        now = time.time()
        for ip in list(devices.keys()):
            try:
                url = f"http://{ip}/data"
                r = requests.get(url, timeout=10)
                if r.status_code == 200:
                    latest_data[ip] = r.json()
                    devices[ip]["last_seen"] = now
            except Exception:
                print(f"[WARN] {ip} unreachable")

        # Remove devices that timed out
        for ip in list(devices.keys()):
            if now - devices[ip]["last_seen"] > DEVICE_TIMEOUT:
                print(f"[REMOVE] {ip} timed out")
                devices.pop(ip)
                latest_data.pop(ip, None)

        time.sleep(SCRAPE_INTERVAL)


# Start scraper in a daemon thread
threading.Thread(target=scraper_loop, daemon=True).start()


# ---------------- API ----------------
@app.route("/api/devices")
def api_devices():
    return jsonify(devices)


@app.route("/api/data")
def api_data():
    return jsonify(latest_data)


# ---------------- MANUAL SCRAPE ----------------
@app.route("/scrape")
def scrape_now():
    global latest_data
    now = time.time()
    for ip in list(devices.keys()):
        try:
            url = f"http://{ip}/data"
            r = requests.get(url, timeout=2)
            if r.status_code == 200:
                latest_data[ip] = r.json()
                devices[ip]["last_seen"] = now
        except Exception:
            print(f"[WARN] {ip} unreachable")
    return jsonify(latest_data)


# ---------------- DASHBOARD ----------------
@app.route("/")
def index():
    return """
    <h1>IoT Auto Discovery Dashboard</h1>

    <h2>Devices</h2>
    <pre id="devices">Loading...</pre>

    <h2>Latest Data</h2>
    <pre id="latest_data">Loading...</pre>

    <script>
    async function fetchData() {
        try {
            const devicesResp = await fetch('/api/devices');
            const dataResp = await fetch('/api/data');

            const devices = await devicesResp.json();
            const data = await dataResp.json();

            document.getElementById('devices').textContent = JSON.stringify(devices, null, 2);
            document.getElementById('latest_data').textContent = JSON.stringify(data, null, 2);
        } catch(err) {
            console.error(err);
        }
    }

    // Fetch every second
    setInterval(fetchData, 1000);
    fetchData(); // initial fetch
    </script>
    """


# ---------------- RUN ----------------
if __name__ == "__main__":
    print(f"Starting Flask on {HOST}:{PORT}")
    app.run(host=HOST, port=PORT, debug=True)