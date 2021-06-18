#!/bin/bash
set -x

#Compile the kernel
cd $KERN_SRC

if [[ $1 == "makemenu" ]];
then
	make menuconfig
fi



#Compile the kernel with '-j' (denotes parallelism) in sudo mode
sudo make  $PARA &> compile.out
grep -r "error:" compile.out &> errors.out
sudo make modules  &>> compile.out
grep -r "error:" compile.out &>> errors.out
sudo make modules_install &>> compile.out
grep -r "error:" compile.out &>> errors.out
sudo make install &>> compile.out
grep -r "error:" compile.out &>> errors.out

 y="4.17.0"
   if [[ x$ == x ]];
  then
      echo You have to say a version!
      exit 1
   fi

sudo cp ./arch/x86/boot/bzImage /boot/vmlinuz-$y
sudo cp System.map /boot/System.map-$y
sudo cp .config /boot/config-$y
sudo update-initramfs -c -k $y
