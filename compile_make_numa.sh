#!/bin/bash -x
set -x
PROC=40
numactl --membind=1 make -j$PROC &>compile.out
grep -r "error:" compile.out &> errors.out
grep -r "undefined:" compile.out &> errors.out
numactl --membind=1 make bzImage -j$PROC &>>compile.out
grep -r "error:" compile.out &> errors.out
grep -r "undefined:" compile.out &> errors.out
numactl --membind=1 make  modules -j$PROC &>>compile.out
numactl --membind=1 make  modules_install -j$PROC &>> compile.out
numactl --membind=1 make install &>> compile.out

 y="4.17.0"	
   if [[ x$ == x ]];
  then
      echo You have to say a version!
      exit 1
   fi

cp ./arch/x86/boot/bzImage /boot/vmlinuz-$y
cp System.map /boot/System.map-$y
cp .config /boot/config-$y
rm -rf /boot/initrd.img-$y
numactl --membind=1 update-initramfs -c -k $y
#echo Now edit menu.lst or run /sbin/update-grub

grep -r "warning:" compile.out &> warnings.out
grep -r "error:" compile.out &> errors.out
grep -r "undefined:" compile.out &> errors.out
set +x
