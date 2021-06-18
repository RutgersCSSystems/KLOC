#!/bin/bash
set +x

$APPBENCH/scripts/umout_qemu.sh
sleep 1
#Launching QEMU
IPADDR=192.168.100.51

$APPBENCH/scripts/killqemu.sh

#sudo qemu-system-x86_64 -kernel $KERNEL/vmlinuz-$VER -hda $QEMU_IMG_FILE -append "root=/dev/sda rw" --enable-kvm -m $QEMUMEM -numa node,nodeid=0,cpus=0-15 -curses -vga std -numa node,nodeid=1,cpus=16-32 

sudo qemu-system-x86_64 -kernel $KERNEL/vmlinuz-$VER -hda $QEMU_IMG_FILE -append "root=/dev/sda rw" --enable-kvm -m $QEMUMEM -numa node,nodeid=0,cpus=0-15 -numa node,nodeid=1,cpus=16-32 -device e1000,netdev=network0,mac=00:00:00:00:00:00 -netdev tap,id=network0,ifname=tap0,script=no,downscript=no -nographic &

sleep 5

ssh $IPADDR
