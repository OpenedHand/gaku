AUTOMAKE_OPTIONS = -Wno-portability

bin_PROGRAMS = gaku

AM_CPPFLAGS = $(DEPS_CFLAGS)
AM_CFLAGS = -Wall
LDADD = $(DEPS_LIBS)

gaku_SOURCES = \
	main.c \
	playlist-parser.c playlist-parser.h

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
