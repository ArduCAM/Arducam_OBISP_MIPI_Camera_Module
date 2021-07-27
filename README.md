# Arducam_OBISP_MIPI_Camera_Module

Arducam OBISP project is to address the problem of lacking ISP support for the RAW sensor, it has On Board ISP as the name implies which has dedicated hardware processor and not relay on the backend processor’s processing capability. With the OBISP camera, users can use better cameras than those natively supported cameras like OV5647, IMX219, IMX477 on Arduino, Raspberry pi, and Jetson Nano without struggling with the ISP things. The first two released OBISP cameras are 13MP AR1335 high-resolution camera and 2MP AR0230 HDR camera. And Sony starvis IMX290, IMX327 camera will come up very soon.

# Driver Support
Current driver supports the following kerner version, for other kernel please send us request for adding support or source code to compile by yourself.
* 4.19.113-v7+
* 4.19.118+
* 4.19.118-v7+
* 4.19.118-v7I+
* 5.4.42-v7+
* 5.4.51+
* 5.4.51-v7+
* 5.4.51-v7l+

For Arducam mipi camera + Pivariety driver, referring to https://github.com/ArduCAM/Arducam-Pivariety-V4L2-Driver
# Support document
https://www.arducam.com/docs/arducam-obisp-mipi-camera-module/

# Video Demo
https://youtu.be/MlRbOJ9n3Q4


# Build driver for other kernel versions (RaspberryPi)
Tested in ubuntu 16.04, 18.04 and 20.04
Run the following command and follow the instructions

* bash build_driver.sh
