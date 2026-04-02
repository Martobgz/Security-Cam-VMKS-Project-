import React from "react";
import { ScrollView, StyleSheet, SafeAreaView } from "react-native";
import CameraFeed   from "../components/CameraFeed";
import ServoControl from "../components/ServoControl";
import SensorData   from "../components/SensorData";

export default function HomeScreen() {
  return (
    <SafeAreaView style={styles.safe}>
      <ScrollView contentContainerStyle={styles.content}>
        <CameraFeed />
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
});
