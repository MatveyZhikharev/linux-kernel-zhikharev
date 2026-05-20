# MiniFS (in-memory filesystem)

This module registers a simple in-memory filesystem named `minifs`.

## Build

```bash
make
```

## Run

```bash
sudo insmod minifs.ko
sudo mkdir -p /mnt/minifs
sudo mount -t minifs none /mnt/minifs

# test
echo hello > /mnt/minifs/file.txt
cat /mnt/minifs/file.txt
ls /mnt/minifs
mkdir /mnt/minifs/dir

# cleanup
sudo umount /mnt/minifs
sudo rmmod minifs
```
