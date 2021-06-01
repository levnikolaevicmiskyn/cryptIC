#!/bin/sh
module="cryptoecho"
device="cryptoecho"
mode="664"

echo $2
/sbin/insmod ./$module.ko $* || exit 1

rm -f /dev/${device}[0-1]

major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)
echo $major
mknod /dev/${device}0 c $major 0
mknod /dev/${device}1 c $major 1

group="staff"
grep -q '^staff:' /etc/group || group="wheel"

chgrp $group /dev/${device}[0-1]
chmod $mode /dev/${device}[0-1]
