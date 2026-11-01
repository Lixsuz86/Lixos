#!/bin/sh
set -e
mkdir -p build
dd if=/dev/zero of=build/disk.img bs=1M count=32
/sbin/mke2fs -t ext2 -b 1024 -F build/disk.img

# Create test files
echo "Hello from Real ext2 Filesystem! Milestone 7.3 Verified." > build/tmp_hello.txt
echo "LIXOS_EXT2_SYSTEM_CONFIG_OK" > build/tmp_conf.txt

# Run debugfs commands non-interactively
debugfs -w build/disk.img << 'EOF'
write build/tmp_hello.txt hello.txt
write build/tmp_conf.txt system.conf
write build/hello.elf hello.elf
write build/sh.elf sh.elf
write build/netclient.elf netclient.elf
sif hello.txt mode 0100644
sif hello.txt uid 1000
sif hello.txt gid 1000
sif system.conf mode 0100600
sif system.conf uid 0
sif system.conf gid 0
sif hello.elf mode 0100755
sif hello.elf uid 1000
sif hello.elf gid 1000
sif sh.elf mode 0100755
sif sh.elf uid 1000
sif sh.elf gid 1000
sif netclient.elf mode 0100755
sif netclient.elf uid 1000
sif netclient.elf gid 1000
ls -l
cat hello.txt
quit
EOF

rm -f build/tmp_hello.txt build/tmp_conf.txt
echo "EXT2_IMAGE_SUCCESS"
