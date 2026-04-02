/**
 * App.js — Entry point
 *
 * Sets up:
 *  - Push notification permissions & Expo token registration
 *  - Bottom-tab navigation (Home / Alerts)
 */
import React, { useEffect, useRef } from "react";
import { Platform, Alert } from "react-native";

import { NavigationContainer } from "@react-navigation/native";
import { createBottomTabNavigator } from "@react-navigation/bottom-tabs";

import * as Device        from "expo-device";
import * as Notifications from "expo-notifications";
import axios              from "axios";

import HomeScreen   from "./src/screens/HomeScreen";
import AlertsScreen from "./src/screens/AlertsScreen";
import { SERVER_URL } from "./src/config";

// ── Notification behaviour while the app is in the foreground ─────────
Notifications.setNotificationHandler({
  handleNotification: async () => ({
    shouldShowAlert: true,
    shouldPlaySound: true,
    shouldSetBadge:  false,
  }),
});

// ── Push-token registration ───────────────────────────────────────────
async function registerForPushNotifications() {
  if (!Device.isDevice) {
    // Emulators cannot receive push notifications
    return;
  }

  const { status: existing } = await Notifications.getPermissionsAsync();
  let finalStatus = existing;

  if (existing !== "granted") {
    const { status } = await Notifications.requestPermissionsAsync();
    finalStatus = status;
  }

  if (finalStatus !== "granted") {
    Alert.alert("Permission denied", "Push notifications will not work.");
    return;
  }

  if (Platform.OS === "android") {
    await Notifications.setNotificationChannelAsync("default", {
      name:       "default",
      importance: Notifications.AndroidImportance.MAX,
      vibrationPattern: [0, 250, 250, 250],
    });
  }

  try {
    const tokenData = await Notifications.getExpoPushTokenAsync();
    const token     = tokenData.data;
    console.log("Expo push token:", token);

    // Register token with Flask
    await axios.post(`${SERVER_URL}/register-push-token`, { token }, { timeout: 5000 });
  } catch (e) {
    console.warn("Push token registration failed:", e.message);
  }
}

// ── Navigation ────────────────────────────────────────────────────────
const Tab = createBottomTabNavigator();

export default function App() {
  const notificationListener = useRef();
  const responseListener     = useRef();

  useEffect(() => {
    registerForPushNotifications();

    // Log received notifications
    notificationListener.current =
      Notifications.addNotificationReceivedListener((notification) => {
        console.log("Notification received:", notification);
      });

    // Handle taps on notifications
    responseListener.current =
      Notifications.addNotificationResponseReceivedListener((response) => {
        console.log("Notification tapped:", response);
      });

    return () => {
      Notifications.removeNotificationSubscription(notificationListener.current);
      Notifications.removeNotificationSubscription(responseListener.current);
    };
  }, []);

  return (
    <NavigationContainer>
      <Tab.Navigator
        screenOptions={{
          tabBarStyle:           { backgroundColor: "#1a1a1a", borderTopColor: "#333" },
          tabBarActiveTintColor: "#00aaff",
          tabBarInactiveTintColor: "#666",
          headerStyle:           { backgroundColor: "#111" },
          headerTintColor:       "#eee",
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
