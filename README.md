# Linux-Filesystem
**A user space file system based on Fuse**
####fuse_file_system/example/hello.c
Check out this .c file to see what is really going on in this filesystem. It is what I have actually written for this project. 

####fsck_sample_files/fsck.py
I wrote a python script to check whether the file system is correctly working, and if not, it would modify it.

####fsck_sample_files/FS
This directory contains the sample blocks which are to support the file system. But they are broken and some information is wrong. It should be 10000 files but for the sake of space I just provide 31. After run the python script the sample blocks should be modified and the file system will operate normally.

