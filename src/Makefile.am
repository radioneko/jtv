bin_PROGRAMS = jtv
noinst_LIBRARIES = libexpat.a librtmp.a
INCLUDES = -Wall @LIBCURL_CPPFLAGS@

libexpat_a_SOURCES = expat/ascii.h expat/asciitab.h expat/expat_external.h expat/expat.h expat/iasciitab.h expat/internal.h expat/latin1tab.h expat/macconfig.h expat/nametab.h expat/utf8tab.h expat/winconfig.h expat/xmlparse.c expat/xmlrole.c expat/xmlrole.h expat/xmltok.c expat/xmltok.h expat/xmltok_impl.c expat/xmltok_impl.h expat/xmltok_ns.c
librtmp_a_SOURCES = librtmp/amf.c librtmp/amf.h librtmp/bytes.h librtmp/dhgroups.h librtmp/dh.h librtmp/handshake.h librtmp/hashswf.c librtmp/http.h librtmp/log.c librtmp/log.h librtmp/parseurl.c librtmp/rtmp.c librtmp/rtmp.h librtmp/rtmp_sys.h

jtv_SOURCES = jtv.c jtv_memory.c jtv_memory.h usher.h usher.c jtv_rtmp.h jtv_rtmp.c
jtv_LDADD = libexpat.a librtmp.a @LIBCURL@

