#!/bin/bash
set -x

$APPBENCH/scripts/umout_qemu.sh
sleep 1
#Launching QEMU

$APPBENCH/scripts/killqemu.sh

MEMORY=$QEMUMEM

sudo qemu-system-x86_64 -kernel $KERNEL/vmlinuz-$VER -hda $QEMU_IMG_FILE -append "root=/dev/sda rw" --enable-kvm -redir tcp:10000::22 -curses

#sudo qemu-system-x86_64 -kernel $KERNEL/vmlinuz-$VER -hda $QEMU_IMG_FILE -append "root=/dev/sda rw" --enable-kvm -m $MEMORY -numa node,nodeid=0,cpus=0-7,mem=$QEMUNODE1 -numa node,nodeid=1,cpus=16-23,mem=$QEMUNODE2 -smp sockets=2,cores=2,threads=2,maxcpus=32 -redir tcp:10000::22 -curses #-net user,hostfwd=tcp::10000-:22

#-nographic #-display curses
#sudo qemu-system-x86_64 -nographic -kernel $KERNEL/vmlinuz-4.17.0 -hda qemu-image.img -append "root=/dev/sda rw console=ttyAMA0 console=ttyS0" --enable-kvm -m 16G -numa node,nodeid=0,cpus=0-4 -numa node,nodeid=1,cpus=10-13
#--curses

