#!/bin/bash

CURRENT_DIR=$(pwd)
echo "Running system update..."
echo "----------------------------------------------------------------------------------------"
sudo apt update -y

echo "----------------------------------------------------------------------------------------"
echo "Installing crossbuild tools..."
echo "----------------------------------------------------------------------------------------"
sudo apt install crossbuild-essential-arm64 libssl-dev git flex bison -y
sudo apt-get install gcc-arm* -y

echo -e "\033[33m A directory named 'Arducam' will be created in your home directory. If you want to change the download path, cancel the process and change the ROOT_PATH variable in the script. \033[0m"
read -p "Press any key to continue..."

ROOT_PATH="${HOME}/Arducam"

[ "$(ls -A ${ROOT_PATH})" ] && echo -e "\033[31m'${ROOT_PATH}' already exists and is not empty. Please change 'ROOT_PATH' in the script to select another directory.\033[0m"

[ -d ${ROOT_PATH} ] && echo "Directory ${ROOT_PATH} exists and is empty. Continuing..." || mkdir ${ROOT_PATH}


cd ${ROOT_PATH}

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error creating directory '${ROOT_PATH}'"
	exit 1
fi


echo "----------------------------------------------------------------------------------------"
echo "Input the branch name of linux kernel that you want to build the driver for"
echo "(Refer to https://github.com/raspberrypi/linux.git and select the correct branch)"
read -p "branch (example: rpi-5.4.y): " select_linux_kernel_branch
echo "----------------------------------------------------------------------------------------"
echo "Running"
echo "git clone -b ${select_linux_kernel_branch} --single-branch https://github.com/raspberrypi/linux.git"
git clone -b $select_linux_kernel_branch --single-branch https://github.com/raspberrypi/linux.git

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error while cloning linux kernel"
	exit 1
fi


echo "----------------------------------------------------------------------------------------"
echo "Cloning camera driver source..."
echo "----------------------------------------------------------------------------------------"
echo "git clone https://github.com/ArduCAM/Arducam_OBISP_MIPI_Camera_Module.git"
git clone https://github.com/ArduCAM/Arducam_OBISP_MIPI_Camera_Module.git

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error while cloning camera driver source"
	exit 1
fi

echo "----------------------------------------------------------------------------------------"
echo "Cloning compile tools for raspberry pi..."
echo "----------------------------------------------------------------------------------------"
echo "git clone git://github.com/raspberrypi/tools.git RpiTools"
git clone git://github.com/raspberrypi/tools.git RpiTools

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error while cloning complire tools for raspberry pi"
	exit 1
fi

echo "----------------------------------------------------------------------------------------"
echo "Adding rpi tools binary to PATH variable in ~/.bashrc ..."
echo "----------------------------------------------------------------------------------------"

grep -qxF "export PATH=$(pwd)/RpiTools/arm-bcm2708/arm-rpi-4.9.3-linux-gnueabihf/bin:\$PATH" ~/.bashrc || echo \
"export PATH=$(pwd)/RpiTools/arm-bcm2708/arm-rpi-4.9.3-linux-gnueabihf/bin:\$PATH" >> ~/.bashrc

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error adding PATH to ~/.bashrc"
	exit 1
fi

source ~/.bashrc

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error sourcing ~/.bashrc"
	exit 1
fi

echo "----------------------------------------------------------------------------------------"
echo "Copying header and source code of camera driver to linux kernel..."
echo "----------------------------------------------------------------------------------------"
cp Arducam_OBISP_MIPI_Camera_Module/sourceCode/arducam.c Arducam_OBISP_MIPI_Camera_Module/sourceCode/arducam.h linux/drivers/media/i2c/

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error copying source code and header files of camera to linux kernel"
	exit 1
fi

echo "----------------------------------------------------------------------------------------"
echo "Copying device tree file of camera driver to linux kernel..."
echo "----------------------------------------------------------------------------------------"
cp Arducam_OBISP_MIPI_Camera_Module/sourceCode/dts/arducam-overlay.dts linux/arch/arm/boot/dts/overlays/

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error copying device tree file to linux kernel"
	exit 1
fi

echo "----------------------------------------------------------------------------------------"
driver_makefile_path="linux/drivers/media/i2c/Makefile"
echo "Updating Makefile at location '${driver_makefile_path}'"
TO_APPEND="obj-\$(CONFIG_VIDEO_ARDUCAM)	+= arducam.o"
TO_FIND="obj-\$(CONFIG_VIDEO_IMX219)"

sed -i "/${TO_FIND}/ a ${TO_APPEND}" ${driver_makefile_path}

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error while updating Makefile"
	exit 1
fi

echo "----------------------------------------------------------------------------------------"
device_tree_makefile_path="linux/arch/arm/boot/dts/overlays/Makefile"
echo "Updating Makefile at location '${device_tree_makefile_path}'"
TO_APPEND="\\\tarducam.dtbo \\\\"
TO_FIND="imx219.dtbo"

sed -i "/${TO_FIND}/ a ${TO_APPEND}" ${device_tree_makefile_path}

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error while updating Makefile"
	exit 1
fi

echo "----------------------------------------------------------------------------------------"
kconfig_path="linux/drivers/media/i2c/Kconfig"
echo "Updating configuration at location '${kconfig_path}'"
TO_FIND="module will be called imx219"
TO_APPEND="\\\nconfig VIDEO_ARDUCAM\\n\\ttristate \"ARDUCAM sensor support\"\\n\\tdepends on I2C && VIDEO_V4L2 && VIDEO_V4L2_SUBDEV_API\\n\\tselect V4L2_FWNODE\\n\\thelp\\n\\t  This is a Video4Linux2 sensor driver for the\\n\\t  arducam camera.\\n\\n\\t  To compile this driver as a module, choose M here: the\\n\\t  module will be called arducam."

sed -i "/${TO_FIND}/ a ${TO_APPEND}" ${kconfig_path}

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error while updating Kconfig"
	exit 1
fi


echo "----------------------------------------------------------------------------------------"
echo "Setting configurations for build..."

cd linux
KERNEL=kernel7l
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- bcm2711_defconfig

TO_FIND="CONFIG_VIDEO_ARDUCAM"
TO_REPLACE="CONFIG_VIDEO_ARDUCAM=m"

sed -i "s/.*${TO_FIND}.*/${TO_REPLACE}/" .config

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error while setting configuration for build"
	exit 1
fi

echo "----------------------------------------------------------------------------------------"
echo "Starting the build process..."

CPU_COUNT=$(grep -c ^processor /proc/cpuinfo)

make -j ${CPU_COUNT} ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage modules dtbs -j8

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error in build"
	exit 1
fi
echo "Build process complete"

echo "----------------------------------------------------------------------------------------"

cd ..
DRIVER_COPY_DIR="driver_${select_linux_kernel_branch}"

echo "Copying compiled driver into ${DRIVER_COPY_DIR}"

mkdir ${DRIVER_COPY_DIR}

cp linux/drivers/media/i2c/arducam.ko ${DRIVER_COPY_DIR}
cp linux/arch/arm/boot/dts/overlays/arducam.dtbo ${DRIVER_COPY_DIR}

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error copying compiled driver files into '${ROOT_PATH}/${DRIVER_COPY_DIR}'. Please copy the files 'linux/drivers/media/i2c/arducam.ko' and 'linux/arch/arm/boot/dts/overlays/arducam.dtbo' on your own."
	exit 1
fi

echo ""
echo "Driver built successfully and saved in '${ROOT_PATH}/${DRIVER_COPY_DIR}'"

echo "----------------------------------------------------------------------------------------"

# echo -e "\033[33mHow To Use the Newly Compiled Driver:"
# echo "  -  check the exact kernel version in your Pi with command 'uname -r'"
# echo "  -  create a directory with exactly the same version name in Arducam_OBISP_MIPI_Camera_Module/Release/bin/"
# echo "  -  copy arducam.dtbo and arducam.ko from '${ROOT_PATH}/${DRIVER_COPY_DIR}' into the newly created directory"
# echo -e "  -  Now you can copy the project Arducam_OBISP_MIPI_Camera_Module into your Pi and run 'install_driver.sh' from Arducam_OBISP_MIPI_Camera_Module/Release directory.\033[0m"

KERNEL_VERSION=$(head -n 1 linux/include/config/kernel.release)
DRIVER_COPY_PATH="${CURRENT_DIR}/Release/bin/${KERNEL_VERSION}"

[ "$(ls -A ${DRIVER_COPY_PATH})" ] && echo "'${DRIVER_COPY_PATH}' already exists and is not empty."
[ -d ${DRIVER_COPY_PATH} ] && echo "Directory ${DRIVER_COPY_PATH} exists. Copying driver files..." || mkdir ${DRIVER_COPY_PATH}

echo "Copying driver files into ${DRIVER_COPY_PATH}"
cp linux/drivers/media/i2c/arducam.ko ${DRIVER_COPY_PATH}
cp linux/arch/arm/boot/dts/overlays/arducam.dtbo ${DRIVER_COPY_PATH}

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error copying driver files into '${DRIVER_COPY_PATH}'"
	exit 1
fi

cd $CURRENT_DIR
echo -e "\033[32mNow you can copy the project Arducam_OBISP_MIPI_Camera_Module into your Pi and run 'install_driver.sh' from Arducam_OBISP_MIPI_Camera_Module/Release directory.\033[0m"
