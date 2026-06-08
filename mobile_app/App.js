/**
 * App.js — Entry point
 *
 * Uses local notifications (works in Expo Go) instead of remote push.
 * Polls /api/alerts every 3 s; fires a local notification when a new
 * alert appears that is newer than the last one we already showed.
 */
import React, { useEffect, useRef } from "react";
import { Platform } from "react-native";

import { NavigationContainer } from "@react-navigation/native";
import { createBottomTabNavigator } from "@react-navigation/bottom-tabs";

import * as Notifications from "expo-notifications";
import axios from "axios";

import HomeScreen   from "./src/screens/HomeScreen";
import AlertsScreen from "./src/screens/AlertsScreen";
import { SERVER_URL } from "./src/config";

// ── Show notifications while app is in foreground ─────────────────────
Notifications.setNotificationHandler({
  handleNotification: async () => ({
    shouldShowAlert: true,
    shouldPlaySound: true,
    shouldSetBadge:  false,
  }),
});

// ── Request notification permissions ─────────────────────────────────
async function requestPermissions() {
  const { status } = await Notifications.requestPermissionsAsync();
  if (Platform.OS === "android") {
    await Notifications.setNotificationChannelAsync("default", {
      name:       "default",
      importance: Notifications.AndroidImportance.MAX,
      vibrationPattern: [0, 250, 250, 250],
    });
  }
  return status === "granted";
}

// ── Build notification text from an alert object ──────────────────────
function alertTitle(alert) {
  if (alert.type === "gas")    return "⚠️ Gas Detected";
  if (alert.type === "motion") return "Person Detected";
  return "Alert";
}

function alertBody(alert) {
  if (alert.type === "gas")
    return `${alert.gas ?? "Gas"} at ${alert.ppm ?? "?"} ppm — check the area!`;
  return "Movement detected — a person is in the frame.";
}

// ── Navigation ────────────────────────────────────────────────────────
const Tab = createBottomTabNavigator();

export default function App() {
  const lastAlertTime = useRef(0);

  useEffect(() => {
    requestPermissions();

    // Poll for new alerts and fire local notifications
    const poller = setInterval(async () => {
      try {
        const res = await axios.get(`${SERVER_URL}/api/alerts`, { timeout: 4000 });
        const alerts = res.data;
        if (!alerts || alerts.length === 0) return;

        const latest = alerts[0];
        if (latest.timestamp > lastAlertTime.current) {
          lastAlertTime.current = latest.timestamp;
          await Notifications.scheduleNotificationAsync({
            content: {
              title: alertTitle(latest),
              body:  alertBody(latest),
              sound: true,
            },
            trigger: null,  // fire immediately
          });
        }
      } catch {
        // silently ignore network errors
      }
    }, 3000);

    return () => clearInterval(poller);
  }, []);

  return (
    <NavigationContainer>
      <Tab.Navigator
        screenOptions={{
          tabBarStyle:             { backgroundColor: "#1a1a1a", borderTopColor: "#333" },
          tabBarActiveTintColor:   "#00aaff",
          tabBarInactiveTintColor: "#666",
          headerStyle:             { backgroundColor: "#111" },
          headerTintColor:         "#eee",
        }}
      >
        <Tab.Screen
          name="Home"
          component={HomeScreen}
          options={{ title: "Camera", tabBarLabel: "Camera" }}
        />
        <Tab.Screen
          name="Alerts"
          component={AlertsScreen}
          options={{ title: "Alerts", tabBarLabel: "Alerts" }}
        />
      </Tab.Navigator>
    </NavigationContainer>
  );
}
