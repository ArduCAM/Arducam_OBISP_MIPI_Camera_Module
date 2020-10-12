import numpy as np
import cv2
import v4l2
import time
import subprocess
cap = cv2.VideoCapture(0)
while(True):
    # Capture frame-by-frame
    subprocess.call(['v4l2-ctl -d /dev/video0 -c ircut=0'],shell=True)
    time.sleep(0.1)
    ret, frame = cap.read()
    cv2.imwrite("image_ircut_0.jpg",frame)
    cv2.imshow('Arducam ar1335 ircut=0',frame)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break
    subprocess.call(['v4l2-ctl -d /dev/video0 -c ircut=1'],shell=True)
    time.sleep(0.1)
    ret, frame2 = cap.read()
    cv2.imwrite("image_ircut_1.jpg",frame2)
    cv2.imshow('Arducam ar1335 ircut=1',frame2)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break   
# When everything done, release the capture
cap.release()
cv2.destroyAllWindows()