# Useful options you might want to put on the make command line:
#
#  - `SLANG=yes' to build against libslang instead of libncurses
#    (libncurses is better and more reliable, but libslang might be
#    all you have on a particular platform if you're unlucky).
#
#  - `XFLAGS=-DNO_LARGE_FILES' to leave out the 64-bit file access
#    support (restricts Tweak to editing files under 2Gb, but
#    should cause it to compile successfully on platforms without
#    fseeko and ftello and/or long long support).
#
#  - `VERSION=X.XX' (for whatever X.XX you like) to cause the `make
#    release' target to build a release tarball called
#    `tweak-X.XX.tar.gz' which unpacks into a directory
#    `tweak-X.XX'. Note that you also need to modify the version
#    number in tweak.h, or else the resulting binary won't match
#    the version number on the archive.

CC := gcc
CFLAGS := -g -c -Wall $(XFLAGS)
LINK := gcc
LFLAGS :=
LIBS := 

PREFIX=$(DESTDIR)/usr/local
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/man/man1

TWEAK := main.o keytab.o actions.o search.o rcfile.o buffer.o btree.o

ifeq ($(SLANG),yes)
# INCLUDE += -I/path/to/slang/include
# LIBS += -L/path/to/slang/lib
LIBS += -lslang
TWEAK += slang.o
else
LIBS += -lncurses
TWEAK += curses.o
endif

.c.o:
	$(CC) $(CFLAGS) $*.c

all: tweak tweak.1 btree.html

tweak:	$(TWEAK)
	$(LINK) -o tweak $(TWEAK) $(LIBS)

tweak.1:  manpage.but
	halibut --man=$@ $<

btree.html:  btree.but
	halibut --html=$@ $<

# Ensure tweak.h reflects this version number, and then run a
# command like `make release VERSION=3.00'.
release: tweak.1 btree.html
	mkdir -p reltmp/tweak-$(VERSION)
	for i in LICENCE *.c *.h *.but tweak.1 btree.html Makefile; do   \
		ln -s ../../$$i reltmp/tweak-$(VERSION);         \
	done
	(cd reltmp; tar chzvf ../tweak-$(VERSION).tar.gz tweak-$(VERSION))
	rm -rf reltmp

install: tweak tweak.1
	mkdir -p $(BINDIR)
	install tweak $(BINDIR)/tweak
	mkdir -p $(MANDIR)
	install -m 0644 tweak.1 $(MANDIR)/tweak.1

clean:
	rm -f *.o tweak tweak.1 btree.html

.PHONY: release clean install

main.o: main.c tweak.h
keytab.o: keytab.c tweak.h
actions.o: actions.c tweak.h
search.o: search.c tweak.h
rcfile.o: rcfile.c tweak.h
buffer.o: buffer.c tweak.h btree.h
slang.o: slang.c tweak.h
curses.o: curses.c tweak.h
btree.o: btree.c btree.h
