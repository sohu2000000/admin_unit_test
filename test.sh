#!/bin/bash

#######################################
# complete kernel modules
#######################################
sudo insmod complete.ko
sudo dmesg -c

awk "\$2==\"complete\" {print \$1}" /proc/devices
sudo mknod /dev/complete0 c 242 0
sudo chgrp "wheel" /dev/complete0
sudo chmod "664"  /dev/complete0

cat /dev/complete0
sudo dmesg -c
echo "defg" > /dev/complete0
sudo dmesg -c

sudo rmmod complete.ko
sudo rm -f /dev/complete0
sudo dmesg -c


#######################################
# Test sleepy modules
#######################################
sudo insmod ./sleepy.ko
sudo dmesg -c

cat /proc/devices | grep class_hello
awk "\$2==\"sleepy\" {print \$1}" /proc/devices
sudo mknod /dev/sleepy c 242 0
sudo chgrp "wheel" /dev/sleepy
sudo chmod "664"  /dev/sleepy

cat /dev/sleepy
sudo dmesg -c
echo "defg" > /dev/sleepy
sudo dmesg -c

sudo rmmod sleepy.ko
sudo rm -f /dev/sleepy
sudo dmesg -c


#######################################
# Test kdatasize module
#######################################
sudo dmesg -c
sudo insmod ./kdatasize.ko
sudo dmesg -c

#######################################
# Test kdataalign module
#######################################
sudo dmesg -c
sudo insmod ./kdataalign.ko
sudo dmesg -c


#######################################
# Test class_hello module
#######################################
sudo dmesg -c
sudo insmod ./class_hello.ko
sudo dmesg -c

awk "\$2==\"class_hello\" {print \$1}" /proc/devices

ll /dev/dev_hello*

ll /sys/class/
ll /sys/class/class_hello/


sudo rmmod ./class_hello.ko
sudo dmesg -c

#######################################
# Test jit module
#######################################
sudo cat /proc/uptime 

sudo dmesg -c

sudo insmod ./jit.ko
sudo dmesg -c

# current time
ll /proc/currentime
cat /proc/currentime
head -8 /proc/currentime
sudo dmesg -c

# delay work
ll /proc/jit*

# load 50 processes
#sudo ../misc-progs/load50

# busy loop
# /proc/jitbusy
cat /proc/jitbusy
sudo dmesg -c
sudo ./read_delay.sh

sudo ../misc-progs/load50
sudo ./read_delay.sh
sudo dmesg -c

# schedule
# /proc/jitsched
cat /proc/jitsched
sudo ./read_delay.sh

sudo ../misc-progs/load50
sudo ./read_delay.sh
sudo dmesg -c


# wait_event_interruptible_timeout
# /proc/jitqueue
cat /proc/jitqueue
sudo ./read_delay.sh

sudo ../misc-progs/load50
sudo ./read_delay.sh
sudo dmesg -c


# wait_event_interruptible_timeout
# /proc/jitschedto
sudo cat /proc/jitschedto
sudo ./read_delay.sh

sudo ../misc-progs/load50
sudo ./read_delay.sh
sudo dmesg -c

# timer 
# /proc/jitimer
sudo cat /proc/jitimer
sudo ../misc-progs/load50
sudo cat /proc/jitimer

sudo rmmod ./jit.ko
sudo dmesg -c

# tasklet 
# /proc/jitasklet & /proc/jitasklethi

cat /proc/jitasklet
cat /proc/jitasklethi

sudo ../misc-progs/load50
cat /proc/jitasklet
cat /proc/jitasklethi

sudo rmmod ./jit.ko
sudo dmesg -c


#######################################
# Test jiq module
#######################################
sudo dmesg -c
sudo insmod ./jiq.ko
ll /proc/jiq*

cat /proc/jiqwq
sudo dmesg -c

cat /proc/jiqwqdelay
sudo dmesg -c

cat /proc/jiqtasklet
sudo dmesg -c

cat /proc/jiqtimer
sudo dmesg -c

sudo rmmod ./jiq.ko
sudo dmesg -c


#######################################
# Test silly module
#######################################
sudo dmesg -c
sudo cat /proc/ioports
sudo cat /proc/iomem

sudo insmod ./silly.ko
sudo dmesg -c

awk "\$2==\"silly\" {print \$1}" /proc/devices

sudo mknod /dev/silly0 c 242 0
sudo chgrp "wheel" /dev/silly0
sudo chmod "664"  /dev/silly0

sudo mknod /dev/silly1 c 242 1
sudo chgrp "wheel" /dev/silly1
sudo chmod "664"  /dev/silly1

sudo mknod /dev/silly2 c 242 2
sudo chgrp "wheel" /dev/silly2
sudo chmod "664"  /dev/silly2

sudo mknod /dev/silly3 c 242 3
sudo chgrp "wheel" /dev/silly3
sudo chmod "664"  /dev/silly3

ll /dev/silly*

cat /dev/silly0
sudo dmesg -c
sudo echo "defg" > /dev/silly0
sudo dmesg -c

cat /dev/silly1
sudo dmesg -c
sudo echo "defg" > /dev/silly1
sudo dmesg -c

cat /dev/silly2
sudo dmesg -c
sudo echo "defg" > /dev/silly2
sudo dmesg -c

cat /dev/silly3
sudo dmesg -c
sudo echo "defg" > /dev/silly3
sudo dmesg -c

sudo rmmod ./silly.ko
sudo rm -f /dev/silly0
sudo rm -f /dev/silly1
sudo rm -f /dev/silly2
sudo rm -f /dev/silly3
sudo dmesg -c



