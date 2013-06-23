include $(top_srcdir)/Makefile.tool-tests.am

SUBDIRS = .
DIST_SUBDIRS = .

dist_noinst_SCRIPTS = filter_stderr

EXTRA_DIST = \
        tiny_tests.stderr.exp tiny_tests.stdout.exp tiny_tests.vgtest

check_PROGRAMS = \
        tiny_tests

AM_CFLAGS   += $(AM_FLAG_M3264_PRI)
AM_CXXFLAGS += $(AM_FLAG_M3264_PRI)
