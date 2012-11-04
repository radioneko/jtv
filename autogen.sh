#! /bin/sh

touch NEWS ChangeLog

echo "Running aclocal..." ; aclocal -Im4 $ACLOCAL_FLAGS || exit 1
echo "Running autoheader..." ; autoheader || exit 1
echo "Running autoconf..." ; autoconf || exit 1
#echo "Running libtoolize..." ; (libtoolize --copy --automake || glibtoolize --automake) || exit 1
echo "Running automake..." ; automake --add-missing --copy || exit 1
./configure ${*}
