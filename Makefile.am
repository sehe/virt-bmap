# virt-bmap
# Copyright (C) 2014 Red Hat Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

CLEANFILES = *~

bin_SCRIPTS = virt-bmap

lib_LTLIBRARIES = \
	virtbmapexaminer.la
virtbmapexaminer_la_CPPFLAGS = \
	-I$(srcdir)
virtbmapexaminer_la_CFLAGS = \
	-Wall \
	-pthread \
	$(GUESTFS_CFLAGS)
virtbmapexaminer_la_LIBADD = \
	$(GUESTFS_LIBS)
virtbmapexaminer_la_LDFLAGS = \
	-module -avoid-version -shared
virtbmapexaminer_la_SOURCES = \
	cleanups.c \
	cleanups.h \
	examiner.c \
	ranges.cpp \
	ranges.h \
	visit.c \
	visit.h
