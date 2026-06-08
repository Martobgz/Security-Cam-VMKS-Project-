/**
 * CameraFeed — fetches the ESP32-CAM's direct IP from Flask,
 * then streams MJPEG straight from the camera (no proxy lag).
 */
import React, { useState, useEffect } from "react";
import { View, Text, ActivityIndicator, StyleSheet } from "react-native";
import { WebView } from "react-native-webview";
import axios from "axios";
import { SERVER_URL } from "../config";

function buildHtml(streamUrl) {
  return `<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    * { margin:0; padding:0; box-sizing:border-box; }
    body { background:#000; display:flex; align-items:center; justify-content:center; height:100vh; overflow:hidden; }
    img  { width:100%; height:100%; object-fit:contain; display:block; transform: rotate(180deg); }
  </style>
</head>
<body>
  <img src="${streamUrl}">
</body>
</html>`;
}

export default function CameraFeed() {
  const [streamUrl, setStreamUrl] = useState(null);
  const [error, setError]         = useState(false);

  useEffect(() => {
    let cancelled = false;

    const resolve = async () => {
      try {
        // Ask Flask for the camera's direct IP
        const res = await axios.get(`${SERVER_URL}/camera/url`, { timeout: 5000 });
        if (cancelled) return;

        if (res.data.stream) {
          setStreamUrl(res.data.stream);   // e.g. http://192.168.0.14/stream
        } else {
          setError(true);
        }
      } catch {
        if (!cancelled) setError(true);
      }
    };

    resolve();
    return () => { cancelled = true; };
  }, []);

  if (error) {
    return (
      <View style={styles.placeholder}>
        <Text style={styles.offline}>📷 Camera offline</Text>
      </View>
    );
  }

  if (!streamUrl) {
    return (
      <View style={styles.placeholder}>
        <ActivityIndicator color="#00aaff" size="large" />
        <Text style={styles.loaderText}>Connecting to camera…</Text>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <WebView
        style={styles.webview}
        originWhitelist={["*"]}
        source={{ html: buildHtml(streamUrl) }}
        scrollEnabled={false}
        bounces={false}
        mediaPlaybackRequiresUserAction={false}
        allowsInlineMediaPlayback
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    width: "100%",
    aspectRatio: 4 / 3,
    backgroundColor: "#000",
    borderRadius: 8,
    overflow: "hidden",
  },
  webview: {
    flex: 1,
    backgroundColor: "#000",
  },
  placeholder: {
    width: "100%",
    aspectRatio: 4 / 3,
    backgroundColor: "#1a1a1a",
    borderRadius: 8,
    alignItems: "center",
    justifyContent: "center",
    gap: 10,
  },
  loaderText: {
    color: "#aaa",
    fontSize: 14,
  },
  offline: {
    color: "#888",
    fontSize: 16,
  },
});
