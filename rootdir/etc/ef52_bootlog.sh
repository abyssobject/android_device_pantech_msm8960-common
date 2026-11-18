#!/system/bin/sh
# EF52 bring-up debug: keep the kernel log, logcat and properties of every boot
# in /cache/ef52-bootlog so they can be read from TWRP when boot hangs before
# ADB works. Rewrites the files every 5 s for 15 minutes, keeps the last 5 boots.
D=/cache/ef52-bootlog
mkdir -p $D
n=$(cat $D/count 2>/dev/null)
n=$((${n:-0} + 1))
echo $n > $D/count
rm -f $D/boot$((n - 5))-*
P=$D/boot$n
cat /proc/version /proc/cmdline > $P-info.txt 2>&1
# early kernel log (reset reason of the previous boot) before the ring buffer wraps
dmesg | head -600 > $P-early-dmesg.txt 2>&1
i=0
while [ $i -lt 180 ]; do
    dmesg > $P-dmesg.txt 2>&1
    logcat -d -b all > $P-logcat.txt 2>&1
    getprop > $P-props.txt 2>&1
    ps -A -o PID,PPID,STAT,NAME > $P-ps.txt 2>&1
    sync
    i=$((i + 1))
    sleep 5
done
