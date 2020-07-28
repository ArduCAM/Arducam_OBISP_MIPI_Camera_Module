# Arducam_OBISP_MIPI_Camera_Module

Arducam OBISP project is to address the problem of lacking ISP support for the RAW sensor, it has On Board ISP as the name implies which has dedicated hardware processor and not relay on the backend processor’s processing capability. With the OBISP camera, users can use better cameras than those natively supported cameras like OV5647, IMX219, IMX477 on Arduino, Raspberry pi, and Jetson Nano without struggling with the ISP things. The first two released OBISP cameras are 13MP AR1335 high-resolution camera and 2MP AR0230 HDR camera. And Sony starvis IMX290, IMX327 camera will come up very soon.

# Driver Support
This repository only provides the driver and camera software tools for Raspberry pi. For Jetson Nano/Xavier driver, please use the [Jetvariety driver](https://github.com/ArduCAM/MIPI_Camera/tree/master/Jetson/Jetvariety/driver) for OBISP cameras.

Current driver supports the following kerner version, for other kernel please send us request for adding support or source code to compile by yourself.
* 4.19.113-v7+
* 4.19.118-v7+
* 4.19.118-v7I+
* 5.4.42-v7+

# Support document
https://www.arducam.com/docs/arducam-obisp-mipi-camera-module/

# Video Demo
https://youtu.be/MlRbOJ9n3Q4
