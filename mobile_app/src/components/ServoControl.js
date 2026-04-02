/**
 * ServoControl — horizontal slider that sends the servo angle to Flask.
 * The Flask server queues it; the Arduino polls and executes it.
 */
import React, { useState, useCallback } from "react";
import { View, Text, StyleSheet } from "react-native";
import Slider from "@react-native-community/slider";
import axios from "axios";
import { SERVER_URL } from "../config";

export default function ServoControl() {
  const [angle, setAngle]   = useState(90);
  const [status, setStatus] = useState("");

  const sendAngle = useCallback(async (value) => {
    const rounded = Math.round(value);
    try {
      await axios.post(`${SERVER_URL}/servo`, { angle: rounded }, { timeout: 3000 });
      setStatus(`Sent ${rounded}°`);
    } catch {
      setStatus("Send failed");
    }
  }, []);

  return (
    <View style={styles.container}>
      <View style={styles.header}>
        <Text style={styles.title}>Servo</Text>
        <Text style={styles.angle}>{Math.round(angle)}°</Text>
        {status ? <Text style={styles.status}>{status}</Text> : null}
      </View>

      <View style={styles.labels}>
        <Text style={styles.label}>0°</Text>
        <Text style={styles.label}>90°</Text>
        <Text style={styles.label}>180°</Text>
      </View>

      <Slider
        style={styles.slider}
        minimumValue={0}
        maximumValue={180}
        step={1}
        value={angle}
        minimumTrackTintColor="#00aaff"
        maximumTrackTintColor="#444"
        thumbTintColor="#00aaff"
        onValueChange={setAngle}
        onSlidingComplete={sendAngle}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    backgroundColor: "#1a1a1a",
    borderRadius: 10,
    padding: 14,
    marginTop: 12,
  },
  header: {
    flexDirection: "row",
    alignItems: "center",
    gap: 10,
    marginBottom: 6,
  },
  title: {
    color: "#eee",
    fontSize: 16,
    fontWeight: "bold",
  },
  angle: {
    color: "#00aaff",
    fontSize: 16,
    fontWeight: "bold",
    minWidth: 46,
  },
  status: {
    color: "#888",
    fontSize: 12,
  },
  labels: {
    flexDirection: "row",
    justifyContent: "space-between",
    paddingHorizontal: 4,
  },
  label: {
    color: "#666",
    fontSize: 11,
  },
  slider: {
    width: "100%",
    height: 40,
  },
});
