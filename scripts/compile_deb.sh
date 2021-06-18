#!/bin/bash -x
cd $KERN_SRC
sudo apt-get install libdpkg-dev kernel-package
export CONCURRENCY_LEVEL=40
export CONCURRENCYLEVEL=40
touch REPORTING-BUGS
#mv .config .config_back
#make distclean
sudo make menuconfig
#fakeroot make-kpkg clean
sudo fakeroot make-kpkg --initrd kernel-image kernel-headers
sudo dpkg -i ../*image*.deb ../*header*.deb
exit
