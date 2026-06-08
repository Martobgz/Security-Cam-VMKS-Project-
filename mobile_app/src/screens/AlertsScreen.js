import React, { useState, useEffect, useCallback } from "react";
import {
  View, Text, FlatList, StyleSheet,
  SafeAreaView, RefreshControl,
} from "react-native";
import axios from "axios";
import { SERVER_URL } from "../config";

const TYPE_COLOR = {
  motion: "#ffaa00",
  person: "#00ff88",
  gas:    "#ff4444",
};

const TYPE_ICON = {
  motion: "🚶",
  person: "🧍",
  gas:    "⚠️",
};

function AlertItem({ item }) {
  const type  = item.type === "gas" ? "gas" : (item.person ? "person" : "motion");
  const color = TYPE_COLOR[type] ?? "#aaa";
  const icon  = TYPE_ICON[type]  ?? "•";
  const date  = new Date(item.timestamp * 1000);
  const time  = date.toLocaleTimeString();
  const date_ = date.toLocaleDateString();

  return (
    <View style={[styles.item, { borderLeftColor: color }]}>
      <Text style={styles.icon}>{icon}</Text>
      <View style={styles.itemBody}>
        <Text style={[styles.itemType, { color }]}>
          {type === "gas"
            ? `${item.gas ?? "Gas"} — ${item.ppm ?? "?"} ppm`
            : type === "person"
            ? "Person detected"
            : "Motion detected"}
        </Text>
        <Text style={styles.itemMeta}>
          {item.device ?? "unknown"} · {time} {date_}
        </Text>
      </View>
    </View>
  );
}

export default function AlertsScreen() {
  const [alerts,      setAlerts]      = useState([]);
  const [refreshing,  setRefreshing]  = useState(false);

  const load = useCallback(async () => {
    try {
      const res = await axios.get(`${SERVER_URL}/api/alerts`, { timeout: 5000 });
      setAlerts(res.data);
    } catch {
      // keep stale list
    }
  }, []);

  useEffect(() => {
    load();
    const id = setInterval(load, 3000);
    return () => clearInterval(id);
  }, [load]);

  const onRefresh = useCallback(async () => {
    setRefreshing(true);
    await load();
    setRefreshing(false);
  }, [load]);

  return (
    <SafeAreaView style={styles.safe}>
      <FlatList
        data={alerts}
        keyExtractor={(_, i) => String(i)}
        renderItem={({ item }) => <AlertItem item={item} />}
        contentContainerStyle={styles.list}
        ListEmptyComponent={
          <Text style={styles.empty}>No alerts yet.</Text>
        }
        refreshControl={
          <RefreshControl refreshing={refreshing} onRefresh={onRefresh} tintColor="#00aaff" />
        }
      />
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safe: {
    flex: 1,
    backgroundColor: "#111",
  },
  list: {
    padding: 12,
    paddingBottom: 30,
  },
  item: {
    flexDirection: "row",
    backgroundColor: "#1a1a1a",
    borderRadius: 8,
    padding: 12,
    marginBottom: 8,
    borderLeftWidth: 4,
    alignItems: "center",
    gap: 10,
  },
  icon: {
    fontSize: 22,
  },
  itemBody: {
    flex: 1,
  },
  itemType: {
    fontSize: 15,
    fontWeight: "bold",
  },
  itemMeta: {
    color: "#666",
    fontSize: 12,
    marginTop: 3,
  },
  empty: {
    color: "#555",
    textAlign: "center",
    marginTop: 60,
    fontSize: 15,
  },
});
