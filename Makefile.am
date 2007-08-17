AUTOMAKE_OPTIONS = -Wno-portability

bin_PROGRAMS = gaku

AM_CPPFLAGS = $(DEPS_CFLAGS)
AM_CFLAGS = -Wall
LDADD = $(DEPS_LIBS)

gaku_SOURCES = \
	main.c \
	audio-player.c audio-player.h \
	playlist-parser.c playlist-parser.h \
	tag-reader.c tag-reader.h \
	marshal.c marshal.h

%.h: %.list
	$(GLIB_GENMARSHAL) --prefix=$* $< --header > $@

%.c: %.list
	$(GLIB_GENMARSHAL) --prefix=$* $< --body > $@

EXTRA_DIST = marshal.list
BUILT_SOURCES = marshal.c marshal.h
CLEANFILES = $(BUILT_SOURCES)

desktopdir = $(datadir)/applications
dist_desktop_DATA = gaku.desktop

MAINTAINERCLEANFILES = \
	aclocal.m4 \
	compile \
	config.guess \
	config.h.in \
	configure \
	depcomp \
	install-sh \
	Makefile.in \
	missing
