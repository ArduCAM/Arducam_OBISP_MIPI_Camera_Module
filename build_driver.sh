#!/bin/bash
set -x

if [ ! ${HOME} ]; then
	echo "No home directory."
	exit 1
fi

echo "Running system update..."
echo "----------------------------------------------------------------------------------------"
sudo apt update -y
if [ $? -eq 0 ]; then
	echo "Updated."
else
	echo "This script is designed to work on the Ubuntu/Debian Linux family."
	exit 1
fi

echo "----------------------------------------------------------------------------------------"
echo "Select architecture:"
echo "	1. 32 bit (arm)"
echo "	2. 64 bit (arm64)"
read -p ": " bit_selection
if [ "${bit_selection}" -eq 1 ]; then
	ARCH="arm"
else
	ARCH="arm64"
fi
echo "${ARCH} selected."

echo "----------------------------------------------------------------------------------------"
echo "Installing compiler tools for ${ARCH}..."
echo "----------------------------------------------------------------------------------------"
if [ ${bit_selection} -eq 1 ]; then
	sudo apt-get install gcc-arm-linux-gnueabihf
else
	apt-get install gcc-aarch64-linux-gnu
fi

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error installing compiler tools"
	exit 1
fi

echo "----------------------------------------------------------------------------------------"
echo "Installing crossbuild tools..."
echo "----------------------------------------------------------------------------------------"
sudo apt install crossbuild-essential-arm64 libssl-dev git flex bison -y
sudo apt-get install gcc-arm* -y

echo "----------------------------------------------------------------------------------------"
DEFAULT_ROOT_PATH=${HOME}/Arducam
echo -e "\033[33m Enter a working directory. \033[0m"
read -p "[default: ${DEFAULT_ROOT_PATH}]: " ROOT_PATH
ROOT_PATH=${ROOT_PATH:-${DEFAULT_ROOT_PATH}}

if [ "$(ls -A ${ROOT_PATH})" ]; then
	echo -e "\033[31m'${ROOT_PATH}' already exists and is not empty.  The build may not work properly. \033[0m"
	read -p "Press any key to continue..."
fi
[ -d ${ROOT_PATH} ] && echo "Directory ${ROOT_PATH} exists and is empty. Continuing..." || mkdir ${ROOT_PATH}


pushd ${ROOT_PATH}

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error creating directory '${ROOT_PATH}'"
	exit 1
fi


echo "----------------------------------------------------------------------------------------"
echo "Input the branch name of linux kernel that you want to build the driver for"
echo "(Refer to https://github.com/raspberrypi/linux.git and select the correct branch)"
read -p "branch [default: rpi-5.10.y]: " select_linux_kernel_branch
select_linux_kernel_branch=${select_linux_kernel_branch:-rpi-5.10.y}
echo "----------------------------------------------------------------------------------------"
echo "Running"
if [ -d linux ]; then
	echo "Repo exists.  Reverting..."
	git -C linux reset --hard HEAD --continue
else
	git clone --depth=1 -b $select_linux_kernel_branch --single-branch https://github.com/raspberrypi/linux.git
fi

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error while cloning linux kernel"
	exit 1
fi


echo "----------------------------------------------------------------------------------------"
echo "Cloning camera driver source..."
echo "----------------------------------------------------------------------------------------"
if [ -d linux ]; then
	echo "Repo exists.  Reverting..."
	git -C Arducam_OBISP_MIPI_Camera_Module reset --hard HEAD --continue
else
	git clone https://github.com/ArduCAM/Arducam_OBISP_MIPI_Camera_Module.git
fi
	
if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error while cloning camera driver source"
	exit 1
fi

echo "----------------------------------------------------------------------------------------"
echo "Copying header and source code of camera driver to linux kernel..."
echo "----------------------------------------------------------------------------------------"
echo "cp Arducam_OBISP_MIPI_Camera_Module/sourceCode/arducam.c Arducam_OBISP_MIPI_Camera_Module/sourceCode/arducam.h linux/drivers/media/i2c/"
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
echo "cp Arducam_OBISP_MIPI_Camera_Module/sourceCode/dts/arducam-overlay.dts linux/arch/${ARCH}/boot/dts/overlays/"
cp Arducam_OBISP_MIPI_Camera_Module/sourceCode/dts/arducam-overlay.dts linux/arch/${ARCH}/boot/dts/overlays/

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
device_tree_makefile_path="linux/arch/${ARCH}/boot/dts/overlays/Makefile"
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
if [ "${bit_selection}" -eq 1 ]; then
	cross_compiler="arm-linux-gnueabihf"
else
	cross_compiler="aarch64-linux-gnu"
fi
pushd linux
KERNEL=kernel7l
make ARCH=${ARCH} CROSS_COMPILE=${cross_compiler}- bcm2711_defconfig

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
CPU_COUNT=$(grep -c ^processor /proc/cpuinfo)
echo "Starting the build process with ${CPU_COUNT} cores..."
make -j ${CPU_COUNT} ARCH=${ARCH} CROSS_COMPILE=${cross_compiler}- zImage modules dtbs

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error in build"
	exit 1
fi
echo "Build process complete"

echo "----------------------------------------------------------------------------------------"
popd

DRIVER_COPY_DIR="Arducam_OBISP_MIPI_Camera_Module/Release/bin/${select_linux_kernel_branch}"
echo "Copying compiled driver into ${DRIVER_COPY_DIR}"
[ -d ${DRIVER_COPY_DIR} ] || mkdir ${DRIVER_COPY_DIR}

cp linux/drivers/media/i2c/arducam.ko ${DRIVER_COPY_DIR}
cp linux/arch/${ARCH}/boot/dts/overlays/arducam.dtbo ${DRIVER_COPY_DIR}

if [ $? -eq 0 ]; then
	echo "Done !!"
else
	echo "error copying compiled driver files into '${DRIVER_COPY_DIR}'. Please copy the files 'linux/drivers/media/i2c/arducam.ko' and 'linux/arch/${ARCH}/boot/dts/overlays/arducam.dtbo' on your own."
	exit 1
fi

echo ""
echo "Driver built successfully and saved in '${DRIVER_COPY_DIR}'"

echo "----------------------------------------------------------------------------------------"
echo -e "\033[32mNow you can copy the project Arducam_OBISP_MIPI_Camera_Module into your Pi and run 'install_driver.sh' from Arducam_OBISP_MIPI_Camera_Module/Release directory.\033[0m"

popd
