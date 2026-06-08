"""
Person detection using OpenCV HOG people detector + Haar cascades as fallback.
Also saves the last processed frame to debug_capture.jpg so you can inspect
exactly what the detector sees.
"""
import cv2
import numpy as np

# HOG full-body detector (built into OpenCV, no downloads needed)
_hog = cv2.HOGDescriptor()
_hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())

# Haar face cascades as fallback
_face_front   = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
_face_profile = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_profileface.xml")


def detect_person(image_bytes: bytes) -> bool:
    arr = np.frombuffer(image_bytes, dtype=np.uint8)
    img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
    if img is None:
        print("[DETECT] could not decode image")
        return False

    img = cv2.resize(img, (640, 480))

    # Camera is mounted upside-down — rotate so detectors see upright image
    img = cv2.rotate(img, cv2.ROTATE_180)

    # Save what the detector actually sees — open debug_capture.jpg to inspect
    cv2.imwrite("debug_capture.jpg", img)

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    gray = cv2.equalizeHist(gray)

    # 1. HOG full-body silhouette detector — works without seeing the face
    bodies, _ = _hog.detectMultiScale(
        img,
        winStride=(8, 8),
        padding=(4, 4),
        scale=1.05,
    )
    if len(bodies) > 0:
        print(f"[DETECT] person=YES — HOG body ({len(bodies)})")
        return True

    # 2. Frontal face cascade fallback
    faces = _face_front.detectMultiScale(
        gray, scaleFactor=1.05, minNeighbors=2, minSize=(20, 20)
    )
    if len(faces) > 0:
        print(f"[DETECT] person=YES — frontal face ({len(faces)})")
        return True

    # 3. Profile face cascade fallback
    profile = _face_profile.detectMultiScale(
        gray, scaleFactor=1.05, minNeighbors=2, minSize=(20, 20)
    )
    if len(profile) > 0:
        print(f"[DETECT] person=YES — profile face ({len(profile)})")
        return True

    print("[DETECT] person=NO")
    return False
