# KLOC


## Please note that we are in the process of adding more scripts, other applications, and better documentation for easing the use of code! Expect more changes soon!


### KLOC Hardware and OS Environment

To enable users to use generally available machine, this documentation will mainly focus on emulated Cloudlab platoform. Users can create a cloudlab instance to run our code (see details below). 

We currently support Ubuntu-based 16.04 kernels and all pacakge installation scripts use debian. While our changes would also run in 18.04 based Ubuntu kernel, due recent change in one of packages (Shim), we can no longer confirm this. Please see Shim discussion below.

#### Getting and Using Ubuntu 16.04 kernel
We encourage users to use NSF CloudLab (see for details). Use the image type "UWMadison744-F18".


#### CloudLab - Partitioning a SSD and downloading the code.
If you are using CrossFS in CloudLab, the root partition is only 16GB for some profiles.
First setup the CloudLab node with SSD and install all the required libraries.

```
lsblk
```

You should see the following indicating the root partition size is very small:
```
NAME   MAJ:MIN RM   SIZE RO TYPE MOUNTPOINT
sda      8:0    0 447.1G  0 disk 
├─sda1   8:1    0    16G  0 part /
├─sda2   8:2    0     3G  0 part 
├─sda3   8:3    0     3G  0 part [SWAP]
└─sda4   8:4    0 425.1G  0 part 
sdb      8:16   0   1.1T  0 disk 
```

We suggest using an Ubuntu 16.04 kernel in the CloudLab. You can use the following profile 
to create a CloudLab node "UWMadison744-F18"

```
git clone https://github.com/RutgersSystems/CloudlabScripts
cd CloudlabScripts
./cloudlab_setup.sh
```
Since /dev/sdc1 is already partitioned, you could just press "q" in fdisk. Otherwise, if you are using another parition, please use the following steps.
Type 'n' and press enter for all other prompts
```
Command (m for help): n
Partition type
   p   primary (0 primary, 0 extended, 4 free)
   e   extended (container for logical partitions)
Select (default p):
....
Last sector, +sectors or +size{K,M,G,T,P} (2048-937703087, default 937703087):
....
Created a new partition 1 of type 'Linux' and of size 447.1 GiB.
```

Now, time to save the partition. When prompted, enter 'w'. Your changes will be persisted.
```
Command (m for help): w
The partition table has been altered.
Calling ioctl() to re-read partition table.
Syncing disks.
.....
```
This will be followed by a script that installs all the required libraries. Please wait patiently 
to complete. Ath the end of it, you will see a mounted SSD partition.


And then after finish, type:

```
findmnt
```

You will see:

```
/users/$Your_User_Name/ssd                 /dev/sdc1   ext4        rw,relatime,data=ordered
```

When compiling our Linux kernel, you will need to reboot the machine. Hence we suggest you also modify /etc/fstab to make sure the ssd partition will be mounted automatically during system boot.

```
sudo vim /etc/fstab

/dev/sda4       /users/$Your_User_Name/ssd     ext4    defaults        0       0
```

#### Changing Max open files 
sudo vim /etc/security/limits.conf

```
root             soft    nofile          1000000
root             hard    nofile          1000000
$Your_User_Name  soft    nofile          1000000
$Your_User_Name  hard    nofile          1000000
```
In addition,
```
sudo sysctl -w fs.file-max=1000000
```

### Environmental variables 

All environmental variables are set in scripts/setvars.sh
```
scripts/setvars.sh
```


### Compiling all kernel shared libraries, and applications

We have an installation script to do this, which works on the Ubuntu machines.

```
scripts/set_appbench.sh
```

Compiling the OS and restarting. 
```
scripts/compile_deb.sh
scripts/compile_nokvm.sh
```

#### Installing the shared library
```
scripts/compile_sharedlib.sh
```

### Compiling and Running RocksDB

```
cd $APPBENCH/apps/RocksDB
./build_rocksdb.sh
./run.sh
```


### Compiling and launching QEMU  (only for QEMU)

From the NVM source directory set the environment variables.
trusty specifies the host systems linux version/codename 
Pass your own OS version name
```
 source scripts/setvars.sh "trusty"   
```
Create the QEMU IMAGE only for the first time. You should 
not create an image (which is your disk now) every time you will be 
compiling and testing your kernel.

During installation, if prompted (y,n), enter yes

```
 scripts/qemu_create.sh  
```

Install the 4.17 kernel with QEMU support and copy kernel files to boot directory
```
  scripts/compile_kern_kvm.sh
```

Now launch the QEMU
```
  scripts/run_qemu.sh
```

### Install the code as a baremetal machine and continue
```
 source scripts/setvars.sh "bionic"
 scripts/compile_kern_kvm.sh
```




### Changing bandwidth of a NUMA node 

Step 1: Run the throttling script

```
 source scripts/setvars.sh 
 $APPBENCH/install_quartz.sh
 $APPBENCH/throttle.sh
```

Step 2: For modifying bandwidth of throttled node, open the following file

```
     vim $APPBENCH/shared_libs/quartz/nvmemul.ini
```

Step 3: Change the read and write to same bandwidth values
```
        bandwidth:
        {
            enable = true;
            model = "/tmp/bandwidth_model";
            read = 5000;
            write = 5000;
        };
   ```
Step 4: Run the throttling script again to check the value

```
 $APPBENCH/throttle.sh
```


