include $(top_srcdir)/Makefile.tool.am

EXTRA_DIST = docs/og-manual.xml

#----------------------------------------------------------------------------
# Headers
#----------------------------------------------------------------------------

pkginclude_HEADERS = \
	objgrind.h

noinst_HEADERS = \
	og_error.h

#----------------------------------------------------------------------------
# objgrind-<platform>
#----------------------------------------------------------------------------

noinst_PROGRAMS  = objgrind-@VGCONF_ARCH_PRI@-@VGCONF_OS@
if VGCONF_HAVE_PLATFORM_SEC
noinst_PROGRAMS += objgrind-@VGCONF_ARCH_SEC@-@VGCONF_OS@
endif

OBJGRIND_SOURCES_COMMON = \
	og_error.c \
	og_main.c

objgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_SOURCES      = \
	$(OBJGRIND_SOURCES_COMMON)
objgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
objgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
objgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_PRI_CAPS@)
objgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_PRI_CAPS@)
objgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
objgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_PRI@ \
	$(LINK) \
	$(objgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS) \
	$(objgrind_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS)

if VGCONF_HAVE_PLATFORM_SEC
objgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_SOURCES      = \
	$(OBJGRIND_SOURCES_COMMON)
objgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
objgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
objgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_SEC_CAPS@)
objgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_SEC_CAPS@)
objgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
objgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_SEC@ \
	$(LINK) \
	$(objgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS) \
	$(objgrind_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS)
endif

# mc_main.c contains the helper function for memcheck that get called
# all the time. To maximise performance compile with -fomit-frame-pointer
# Primary beneficiary is x86.
og_main.o: CFLAGS += -fomit-frame-pointer

