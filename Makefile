CC=gcc
CFLAGS=-pedantic -Wall -O2 -s -Iinclude -fPIC
DESTDIR=
PREFIX=usr/local
PKGCONFIG=$(DESTDIR)/$(PREFIX)/lib/pkgconfig

.PHONY: all clean demos

OBJS=src/common.o src/buffer.o src/draw.o src/unix.o src/unix_input.o src/unix_hints.o

all: library

library: static-library dynamic-library

static-library: lib/libhexes.a
dynamic-library: lib/libhexes.so.0 lib/libhexes.so

# (Un)install.
install: library include/hexes.h
	mkdir -p $(DESTDIR)/$(PREFIX)/include
	mkdir -p $(DESTDIR)/$(PREFIX)/lib
	mkdir -p $(PKGCONFIG)
	cp -P lib/libhexes.a lib/libhexes.so* $(DESTDIR)/$(PREFIX)/lib
	cp include/hexes.h $(DESTDIR)/$(PREFIX)/include
	@echo "prefix=/$(PREFIX)" > $(PKGCONFIG)/hexes.pc
	@echo 'exec_prefix=$${prefix}' >> $(PKGCONFIG)/hexes.pc
	@echo 'libdir=$${exec_prefix}/lib' >> $(PKGCONFIG)/hexes.pc
	@echo 'includedir=$${prefix}/include' >> $(PKGCONFIG)/hexes.pc
	@echo '' >> $(PKGCONFIG)/hexes.pc
	@echo 'Name: hexes' >> $(PKGCONFIG)/hexes.pc
	@echo 'Version:' $$(cat VERSION) >> $(PKGCONFIG)/hexes.pc
	@echo 'Description: A low-level terminal control library, including optimization.' >> $(PKGCONFIG)/hexes.pc
	@echo '' >> $(PKGCONFIG)/hexes.pc
	@echo 'Libs: -L$${libdir} -lhexes' >> $(PKGCONFIG)/hexes.pc
	@echo 'Cflags: -I$${includedir}' >> $(PKGCONFIG)/hexes.pc

uninstall:
	rm -f $(DESTDIR)/$(PREFIX)/lib/libhexes.a
	rm -f $(DESTDIR)/$(PREFIX)/lib/libhexes.so*
	rm -f $(DESTDIR)/$(PREFIX)/include/hexes.h
	rm -f $(PKGCONFIG)/hexes.pc

# Libraries
lib/libhexes.a:	$(OBJS)
	@mkdir -p $(@D)
	ar cr $@ $(OBJS)
lib/libhexes.so:	lib/libhexes.so.0
	ln -s libhexes.so.0 $@
lib/libhexes.so.0:	$(OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -shared -o $@ $(OBJS)

# Demos	
demos:
	$(MAKE) -C demos

# Common
.c.o: include/hexes.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf lib/ $(OBJS)
	$(MAKE) -C demos clean
