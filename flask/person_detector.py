"""
Person detection using MediaPipe Pose.
Returns True when a human body is detected in the image bytes.
"""
import numpy as np
import cv2
import mediapipe as mp

_mp_pose = mp.solutions.pose


def detect_person(image_bytes: bytes) -> bool:
    """
    Decode a JPEG image and return True if MediaPipe detects a person.
    Falls back to OpenCV HOG detector if MediaPipe gives no result.
    """
    arr = np.frombuffer(image_bytes, dtype=np.uint8)
    img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
    if img is None:
        return False

    # --- MediaPipe Pose ---
    img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    with _mp_pose.Pose(
        static_image_mode=True,
        model_complexity=0,
        min_detection_confidence=0.5,
    ) as pose:
        result = pose.process(img_rgb)
        if result.pose_landmarks is not None:
            return True

    # --- OpenCV HOG fallback ---
    hog = cv2.HOGDescriptor()
    hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())
    rects, _ = hog.detectMultiScale(img, winStride=(8, 8), padding=(4, 4), scale=1.05)
    return len(rects) > 0
