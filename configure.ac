AC_PREREQ(2.58)
AC_INIT(jtv_view, 0.1, kawaii.neko@pochta.ru)
AM_INIT_AUTOMAKE(1.9, dist-bzip2, no-define)

AC_CONFIG_SRCDIR(Makefile.in)
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADER(jtv_config.h)


AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_RANLIB

AC_HEADER_STDC
AC_C_CONST
AC_TYPE_SIZE_T
AC_CHECK_FUNCS(memmove bcopy)

AC_DEFINE([XML_NS], 0,
	[Define to make XML Namespaces functionality available.])
AC_DEFINE([XML_DTD], 1,
	[Define to make parameter entity parsing functionality available.])
AC_DEFINE([XML_CONTEXT_BYTES], 1024,
	[Define to specify how much context to retain around the current parse point.])

LIBCURL_CHECK_CONFIG(,7.10)

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
