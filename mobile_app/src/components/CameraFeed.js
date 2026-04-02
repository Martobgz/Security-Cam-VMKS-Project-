/**
 * CameraFeed — polls /camera/snapshot every 350 ms and displays it.
 * Using snapshot polling instead of native MJPEG because React Native
 * does not support multipart streams in <Image>.
 */
import React, { useState, useEffect, useRef } from "react";
import { View, Image, Text, ActivityIndicator, StyleSheet } from "react-native";
import { SERVER_URL } from "../config";

export default function CameraFeed() {
  const [uri, setUri]           = useState(null);
  const [online, setOnline]     = useState(false);
  const [loading, setLoading]   = useState(true);
  const intervalRef             = useRef(null);

  useEffect(() => {
    let mounted = true;

    const poll = () => {
      // Append timestamp to bust the cache on every request
      const next = `${SERVER_URL}/camera/snapshot?t=${Date.now()}`;
      Image.prefetch(next)
        .then(() => {
          if (!mounted) return;
          setUri(next);
          setOnline(true);
          setLoading(false);
        })
        .catch(() => {
          if (!mounted) return;
          setOnline(false);
          setLoading(false);
        });
    };

    poll();
    intervalRef.current = setInterval(poll, 350);
    return () => {
      mounted = false;
      clearInterval(intervalRef.current);
    };
  }, []);

  if (loading) {
    return (
      <View style={styles.placeholder}>
        <ActivityIndicator color="#00aaff" size="large" />
        <Text style={styles.label}>Connecting to camera…</Text>
      </View>
    );
  }

  if (!online) {
    return (
      <View style={styles.placeholder}>
        <Text style={styles.offline}>📷 Camera offline</Text>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <Image
        source={{ uri, cache: "reload" }}
        style={styles.image}
        resizeMode="contain"
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
  image: {
    width: "100%",
    height: "100%",
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
  label: {
    color: "#aaa",
    fontSize: 14,
  },
  offline: {
    color: "#888",
    fontSize: 16,
  },
});
