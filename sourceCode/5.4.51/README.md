# Build the kernel driver on the Raspberry Pi

### Download the kernel source code
```Bash
cd ~
sudo apt update && sudo apt install git bc bison flex libssl-dev
sudo wget https://raw.githubusercontent.com/RPi-Distro/rpi-source/master/rpi-source -O /usr/local/bin/rpi-source && sudo chmod +x /usr/local/bin/rpi-source && /usr/local/bin/rpi-source -q --tag-update
mkdir $(uname -r)
rpi-source -d $(uname -r)
```

### Build Arducam_OBISP_MIPI_Camera Driver

```Bash
git clone https://github.com/ArduCAM/Arducam_OBISP_MIPI_Camera_Module.git
cd Arducam_OBISP_MIPI_Camera_Module/sourceCode/5.4.51

# compile dtbo
patch -p1 -d /lib/modules/$(uname -r)/build -i $(pwd)/arducam_device_tree.patch
make -C /lib/modules/$(uname -r)/build  dtbs
sudo cp /lib/modules/$(uname -r)/build/arch/arm/boot/dts/overlays/arducam.dtbo /boot/overlays/arducam.dtbo

# manully add dtparam=i2c_vc=on in /boot/config.txt
# manully add dtoverlay=arducam in /boot/config.txt
# Editing /boot/config.txt needs to be reboot to take effect.

# compile arducam.ko
make clean
make && sudo make install
sudo depmod
sudo modprobe arducam
```
