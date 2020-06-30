#!/bin/bash
# load i2c-dev moudle
echo "--------------------------------------"
echo "Enable i2c0 adapter..."
echo "--------------------------------------"
sudo modprobe i2c-dev
# add dtparam=i2c_vc=on to /boot/config.txt
awk 'BEGIN{ count=0 }       \
{                           \
    if($1 == "dtparam=i2c_vc=on"){       \
        count++;            \
    }                       \
}END{                       \
    if(count <= 0){         \
        system("sudo sh -c '\''echo dtparam=i2c_vc=on >> /boot/config.txt'\''"); \
    }                       \
}' /boot/config.txt
echo "Add dtoverlay=arducam to /boot/config.txt "
echo "--------------------------------------"
awk 'BEGIN{ count=0 }       \
{                           \
    if($1 == "dtoverlay=arducam"){       \
        count++;            \
    }                       \
}END{                       \
    if(count <= 0){         \
        system("sudo sh -c '\''echo dtoverlay=arducam >> /boot/config.txt'\''"); \
    }                       \
}' /boot/config.txt
echo "Add gpu=400M to /boot/config.txt "
awk 'BEGIN{ count=0 }       \
{                           \
    if($1 == "gpu_mem=400"){       \
        count++;            \
    }                       \
}END{                       \
    if(count <= 0){         \
        system("sudo sh -c '\''echo gpu_mem=400 >> /boot/config.txt'\''"); \
    }                       \
}' /boot/config.txt
echo "Add cma=128M to /boot/cmdline.txt "
echo "--------------------------------------"
sudo sed 's/cma=128M//g' -i /boot/cmdline.txt
sudo sed 's/[[:blank:]]*$//' -i /boot/cmdline.txt
sudo sed 's/$/& cma=128M/g' -i /boot/cmdline.txt
echo "Installing the arducam.ko driver"
echo "--------------------------------------"
sudo install -p -m 644 ./bin/$(uname -r)/arducam.ko  /lib/modules/$(uname -r)/kernel/drivers/media/i2c/
sudo install -p -m 644 ./bin/$(uname -r)/arducam.dtbo /boot/overlays/
sudo /sbin/depmod -a $(uname -r)
sudo install -p -m 777 ./arducamstill/arducamstill /usr/bin
echo "reboot now?(y/n):"
read USER_INPUT
case $USER_INPUT in
'y'|'Y')
    echo "reboot"
    sudo reboot
;;
*)
    echo "cancel"
;;
esac

        
