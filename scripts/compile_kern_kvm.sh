#!/bin/bash
set -x

sudo umount $MOUNT_DIR
#Compile the kernel
cd $KERN_SRC
#Enable the KVM mode in your kernel config file
#sudo make x86_64_defconfig
#sudo make kvmconfig 
#Compile the kernel with '-j' (denotes parallelism) in sudo mode
sudo make $PARA &> $KERN_SRC/compile.out
grep -r "error:|undefined|warning" $KERN_SRC/compile.out &> $KERN_SRC/errors.out
#sudo make modules &>> $KERN_SRC/compile.out
#sudo make modules_install &>> $KERN_SRC/compile.out
#grep -r "error:|undefined" $KERN_SRC/compile.out &>> $KERN_SRC/errors.out
#sudo make install &>> $KERN_SRC/compile.out
#grep -r "error:|undefined" $KERN_SRC/compile.out &>> $KERN_SRC/errors.out

 y="4.17.0"
   if [[ x$ == x ]];
  then
      echo You have to say a version!
      exit 1
   fi

sudo cp ./arch/x86/boot/bzImage $KERNEL/vmlinuz-$y
sudo cp System.map $KERNEL/System.map-$y
sudo cp .config $KERNEL/config-$y
#sudo update-initramfs -c -k $y
grep -r "error:" $KERN_SRC/compile.out &> $KERN_SRC/errors.out
cat $KERN_SRC/errors.out

