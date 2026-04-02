// ─── Server configuration ─────────────────────────────────────────────────────
// Change FLASK_IP to the local IP of the PC running Flask.
// This must be reachable from your phone (same WiFi network).
export const FLASK_IP   = "192.168.0.77";
export const FLASK_PORT = 5000;

export const SERVER_URL = `http://${FLASK_IP}:${FLASK_PORT}`;
