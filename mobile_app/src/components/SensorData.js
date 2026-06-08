/**
 * SensorData — polls /api/data every 2 s and renders sensor readings.
 */
import React, { useState, useEffect } from "react";
import { View, Text, StyleSheet } from "react-native";
import axios from "axios";
import { SERVER_URL } from "../config";

const DANGER = { lpg: 1000, co: 50, smoke: 300 };

function Card({ label, value, unit, danger }) {
  return (
    <View style={[styles.card, danger && styles.cardDanger]}>
      <Text style={styles.cardLabel}>{label}</Text>
      <Text
        style={[styles.cardValue, danger && styles.valueDanger]}
        numberOfLines={1}
        adjustsFontSizeToFit
        minimumFontScale={0.6}
      >
        {value ?? "—"}{unit ? ` ${unit}` : ""}
      </Text>
    </View>
  );
}

export default function SensorData() {
  const [data, setData] = useState(null);

  useEffect(() => {
    let mounted = true;
    const fetch = async () => {
      try {
        const res = await axios.get(`${SERVER_URL}/api/data`, { timeout: 4000 });
        if (!mounted) return;
        // Flatten the first (and usually only) sensor device's readings
        const values = Object.values(res.data)[0] ?? null;
        setData(values);
      } catch {
        // silently ignore; stale data remains displayed
      }
    };
    fetch();
    const id = setInterval(fetch, 2000);
    return () => { mounted = false; clearInterval(id); };
  }, []);

  if (!data) {
    return (
      <View style={styles.container}>
        <Text style={styles.heading}>Sensors</Text>
        <Text style={styles.noData}>Waiting for sensor data…</Text>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <Text style={styles.heading}>Sensors</Text>
      <View style={styles.grid}>
        <Card
          label="Motion"
          value={data.motion === 1 ? "DETECTED" : "Clear"}
          danger={data.motion === 1}
        />
        <Card label="Light" value={data.light} unit="raw" />
        <Card label="LPG"   value={data.lpg}   unit="ppm" danger={data.lpg   > DANGER.lpg}   />
        <Card label="CO"    value={data.co}    unit="ppm" danger={data.co    > DANGER.co}    />
        <Card label="Smoke" value={data.smoke} unit="ppm" danger={data.smoke > DANGER.smoke} />
        <Card label="Servo" value={data.servo} unit="°"   />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    marginTop: 12,
  },
  heading: {
    color: "#00aaff",
    fontSize: 16,
    fontWeight: "bold",
    marginBottom: 8,
  },
  noData: {
    color: "#666",
    fontSize: 13,
  },
  grid: {
    flexDirection: "row",
    flexWrap: "wrap",
  },
  card: {
    backgroundColor: "#1a1a1a",
    borderRadius: 8,
    padding: 12,
    margin: 4,
    flex: 1,
    minWidth: "44%",
  },
  cardDanger: {
    backgroundColor: "#3a1010",
    borderColor: "#ff4444",
    borderWidth: 1,
  },
  cardLabel: {
    color: "#888",
    fontSize: 11,
    marginBottom: 4,
    width: "100%",
    textAlign: "center",
  },
  cardValue: {
    color: "#eee",
    fontSize: 16,
    fontWeight: "bold",
    width: "100%",
    textAlign: "center",
  },
  valueDanger: {
    color: "#ff6666",
  },
});
