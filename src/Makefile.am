bin_PROGRAMS = jtv
noinst_LIBRARIES = libexpat.a
INCLUDES = -Wall @LIBCURL_CPPFLAGS@

libexpat_a_SOURCES = expat/ascii.h expat/asciitab.h expat/expat_external.h expat/expat.h expat/iasciitab.h expat/internal.h expat/latin1tab.h expat/macconfig.h expat/nametab.h expat/utf8tab.h expat/winconfig.h expat/xmlparse.c expat/xmlrole.c expat/xmlrole.h expat/xmltok.c expat/xmltok.h expat/xmltok_impl.c expat/xmltok_impl.h expat/xmltok_ns.c

jtv_SOURCES = jtv.c jtv_memory.c jtv_memory.h usher.h usher.c
jtv_LDADD = libexpat.a @LIBCURL@

