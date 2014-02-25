rloop
=====

Reverse loopback device - presents a directory as a FAT16/32 image.

Example:


mkdir mount1

# N.B. size-of-image should be at least large enough to contain the
# hierarchy rooted at /chosen/data/directory.
rloop mount1 -D /chosen/data/directory -S size-of-image

ls mount1
# => disk.img

cp mount1/disk.img .
sudo losetup /dev/loop0 disk.img

mkdir mount2
sudo mount /dev/loop0 mount2
ls mount2

# => contents of /chosen/data/directory :-)


That's about as much as it does right now.
