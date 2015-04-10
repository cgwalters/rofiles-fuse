fuse-rofiles:
	gcc -o rofiles-fuse -Wall -std=c99 -g -ggdb -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -lfuse main.c
