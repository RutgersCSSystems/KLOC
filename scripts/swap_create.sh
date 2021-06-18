#!/bin/bash
sudo fallocate -l 2G /users/kannan11/ssd/swapfile
sudo dd if=/dev/zero of=/users/kannan11/ssd/swapfile bs=1024 count=1048576
sudo chmod 600 /users/kannan11/ssd/swapfile
sudo mkswap /users/kannan11/ssd/swapfile
sudo swapon /users/kannan11/ssd/swapfile
