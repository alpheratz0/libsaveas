.POSIX:
.PHONY: all clean install example

include config.mk

all: libsaveas.a

saveas.o: saveas.c saveas.h

libsaveas.a: saveas.o
	$(AR) -rcs libsaveas.a saveas.o

example: libsaveas.a
	$(CC) example.c -o example -L. -I.. -lxcb -lxcb-image -lxcb-xkb -lxcb-keysyms -lxcb-cursor -lxcb-icccm -lsaveas

install: libsaveas.a
	rm -rf $(PREFIX)/include/libsaveas
	mkdir $(PREFIX)/include/libsaveas
	cp saveas.h $(PREFIX)/include/libsaveas
	cp libsaveas.a $(PREFIX)/lib

clean:
	rm -f saveas.o libsaveas.a example