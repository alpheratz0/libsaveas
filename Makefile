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
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	mkdir -p $(DESTDIR)$(PREFIX)/include/saveas
	cp -f libsaveas.a $(DESTDIR)$(PREFIX)/lib
	cp -f saveas.h $(DESTDIR)$(PREFIX)/include/saveas

clean:
	rm -f saveas.o libsaveas.a example
