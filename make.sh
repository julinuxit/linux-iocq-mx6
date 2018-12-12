#####################################
#                                   #
# Discription : KERNEL BUILD SCRIPT #
# Date      : 2017.2.21             #
# Author    : LinuxIT, Inc.         #
#                                   #
#####################################


#!/bin/bash

# Kernel Build

CROSS_COMPILE=arm-linux-gnueabihf-

ARCH=arm

CORE=$(($(grep processor /proc/cpuinfo | awk '{field=$NF};END{print field+1}')*2))

TARGET_BOARD=litmx6iocq_r03

KERNEL_VARIANT=iocq

##### Kernel object file path #####

OBJ_PATH=../obj_imx_3.14.28_1.0.0_ga


##### kernel module install path #####

#INSTALL_MODULE_PATH=/media/$(hostname)/rootfs_IOCQ
INSTALL_MODULE_PATH=/media/$(hostname)/rootfs_IOCQ_RA
#INSTALL_MODULE_PATH=/media/$(hostname)/IOCQ_eMMC
#INSTALL_MODULE_PATH=/media/$(hostname)/DS-BBXGW_root

##### kernel install path #####

INSTALL_PATH=$INSTALL_MODULE_PATH/boot

INSTALL_HDR_PATH=../inc_imx_3.14.28_1.0.0_ga

EXTRAVERSION=$TARGET_BOARD

INSTALL_HDR_PATH=$INSTALL_MODULE_PATH

case $1 in
	"install")
		echo "instaling....."
	
		if [ -d $INSTALL_MODULE_PATH ]; then
			# Install zImage & Copy
			sudo INSTALL_PATH=$INSTALL_PATH INSTALL_MOD_PATH=$INSTALL_MODULE_PATH make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$OBJ_PATH -j$CORE $1
			echo "Kernel Copy"
			echo "sudo cp $OBJ_PATH/arch/arm/boot/zImage $INSTALL_MODULE_PATH/boot/"
			sudo cp $OBJ_PATH/arch/arm/boot/zImage $INSTALL_MODULE_PATH/boot/

			# dtb file copy
			echo "sudo cp ./arch/arm/boot/dts/imx6q-$TARGET_BOARD.dtb $INSTALL_MODULE_PATH/boot/"
			sudo cp $OBJ_PATH/arch/arm/boot/dts/imx6q-$TARGET_BOARD.dtb $INSTALL_MODULE_PATH/boot/
		else
			echo "Invalid install path: $INSTALL_MODULE_PATH"
			exit
		fi
		;;
	"defconfig")
		KERNEL_VARIANT=$KERNEL_VARIANT EXTRAVERSION=$EXTRAVERSION INSTALL_PATH=$INSTALL_PATH INSTALL_MOD_PATH=$INSTALL_MODULE_PATH make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$OBJ_PATH -j$CORE ${TARGET_BOARD}_defconfig
		;;
	"make")
		# Kernel Build
		echo "Build Path: $OBJ_PATH"
		KERNEL_VARIANT=$KERNEL_VARIANT EXTRAVERSION=$EXTRAVERSION INSTALL_PATH=$INSTALL_PATH INSTALL_MOD_PATH=$INSTALL_MODULE_PATH make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$OBJ_PATH -j$CORE all
		;;
	"mrproper")
		EXTRAVERSION=$EXTRAVERSION INSTALL_PATH=$INSTALL_PATH INSTALL_MOD_PATH=$INSTALL_MODULE_PATH make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$OBJ_PATH -j$CORE $1
		;;
	"distclean")
		EXTRAVERSION=$EXTRAVERSION INSTALL_PATH=$INSTALL_PATH INSTALL_MOD_PATH=$INSTALL_MODULE_PATH make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$OBJ_PATH -j$CORE $1
		;;
	"clean")
		EXTRAVERSION=$EXTRAVERSION INSTALL_PATH=$INSTALL_PATH INSTALL_MOD_PATH=$INSTALL_MODULE_PATH make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$OBJ_PATH -j$CORE $1
		;;
	"menuconfig")
		KERNEL_VARIANT=$KERNEL_VARIANT EXTRAVERSION=$EXTRAVERSION INSTALL_PATH=$INSTALL_PATH INSTALL_MOD_PATH=$INSTALL_MODULE_PATH make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$OBJ_PATH -j$CORE $1
		;;
	"modules_install")
		sudo EXTRAVERSION=$EXTRAVERSION INSTALL_PATH=$INSTALL_PATH INSTALL_MOD_PATH=$INSTALL_MODULE_PATH make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$OBJ_PATH -j$CORE $1
		;;
	"firmware_install")
		sudo EXTRAVERSION=$EXTRAVERSION INSTALL_PATH=$INSTALL_PATH INSTALL_MOD_PATH=$INSTALL_MODULE_PATH make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$OBJ_PATH -j$CORE $1
		;;
	"headers_install")
		export INSTALL_HDR_PATH=$INSTALL_HDR_PATH
		INSTALL_HDR_PATH=$INSTALL_HDR_PATH EXTRAVERSION=$EXTRAVERSION INSTALL_PATH=$INSTALL_PATH INSTALL_MOD_PATH=$INSTALL_MODULE_PATH make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$OBJ_PATH -j$CORE $1
		;;
	*)
		echo "Usage: "
		echo "	$0 mrproper		# Remove Dependency Files"
		echo "	$0 distclean		# Remove Board Config"	
		echo "	$0 clean			# Remove Object Files"
		echo "	$0 menuconfig		# Board Config : menuconfig"	
		echo "	$0 defconfig		# Board Config : $TARGET_BOARD"
		echo "	$0 make			# Source Compile"
		echo "	$0 install		# zImage Install to Target Board"
		echo "	$0 modules_install	# Kernel Modules Install to Target Board"
		echo "	$0 firmware_install	# Kernel Firmware Install to Target Board"
		echo "	$0 headers_install	# Kernel Header Files Install to Target Board"
		exit 
		;;
	
esac
