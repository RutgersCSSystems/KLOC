#!/bin/bash -x
sudo apt-get install -y libdpkg-dev kernel-package
export CONCURRENCY_LEVEL=40
export CONCURRENCYLEVEL=40
touch REPORTING-BUGS
#mv .config .config_back
#make distclean
#make menuconfig
#fakeroot make-kpkg clean
sudo fakeroot make-kpkg -j40 --initrd kernel-image kernel-headers
sudo dpkg -i ../*image*.deb ../*header*.deb
exit
