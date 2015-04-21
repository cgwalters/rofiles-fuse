fuse-rofiles:
	gcc -o rofiles-fuse $$(pkg-config --cflags --libs glib-2.0) -O0 -Wall -std=c99 -g -ggdb -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -lfuse main.c

install:
	install -m 755 -D rofiles-fuse $(DESTDIR)/usr/bin/rofiles-fuse


# blah
