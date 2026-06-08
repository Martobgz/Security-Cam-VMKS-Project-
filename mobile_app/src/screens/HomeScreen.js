import React, { useState, useEffect, useCallback } from "react";
import { ScrollView, StyleSheet, SafeAreaView, View, Text, Switch } from "react-native";
import axios from "axios";
import CameraFeed   from "../components/CameraFeed";
import ServoControl from "../components/ServoControl";
import SensorData   from "../components/SensorData";
import { SERVER_URL } from "../config";

function DetectionToggle() {
  const [enabled, setEnabled] = useState(false);

  // Sync with server on mount
  useEffect(() => {
    axios.get(`${SERVER_URL}/detection-status`, { timeout: 4000 })
      .then(res => setEnabled(res.data.enabled))
      .catch(() => {});
  }, []);

  const toggle = useCallback(async (value) => {
    setEnabled(value);
    try {
      await axios.post(`${SERVER_URL}/set-detection`, { enabled: value }, { timeout: 4000 });
    } catch {
      setEnabled(!value); // revert on failure
    }
  }, []);

  return (
    <View style={styles.toggleRow}>
      <View>
        <Text style={styles.toggleLabel}>Person Detection</Text>
        <Text style={styles.toggleSub}>
          {enabled ? "ON — alerts when a person is detected" : "OFF — motion alerts disabled"}
        </Text>
      </View>
      <Switch
        value={enabled}
        onValueChange={toggle}
        trackColor={{ false: "#333", true: "#005588" }}
        thumbColor={enabled ? "#00aaff" : "#666"}
      />
    </View>
  );
}

export default function HomeScreen() {
  return (
    <SafeAreaView style={styles.safe}>
      <ScrollView contentContainerStyle={styles.content}>
        <CameraFeed />
        <DetectionToggle />
        <ServoControl />
        <SensorData />
      </ScrollView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safe: {
    flex: 1,
    backgroundColor: "#111",
  },
  content: {
    padding: 12,
    paddingBottom: 30,
  },
  toggleRow: {
    flexDirection: "row",
    alignItems: "center",
    justifyContent: "space-between",
    backgroundColor: "#1a1a1a",
    borderRadius: 10,
    padding: 14,
    marginTop: 12,
  },
  toggleLabel: {
    color: "#eee",
    fontSize: 16,
    fontWeight: "bold",
  },
  toggleSub: {
    color: "#666",
    fontSize: 12,
    marginTop: 3,
    maxWidth: 220,
  },
});
