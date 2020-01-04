# -----------------------------------------------------------------------------
# Initialize everything
# -----------------------------------------------------------------------------

AC_INIT([gentoo], [0.20.7], [emil@obsession.se], , http://obsession.se/gentoo/)
AC_CONFIG_SRCDIR([src/gentoo.c])
AM_INIT_AUTOMAKE
          
AC_USE_SYSTEM_EXTENSIONS

AC_DEFINE(GTK_DISABLE_DEPRECATED, [1], [Makes GTK+ disable all backward support for old widgets and API:s.])
AC_DEFINE(GTK_DISABLE_COMPAT_H, [1], [Makes GTK+ disable old 1.0.6 compatibility.])

DATE=`date +%Y-%m-%d`

AC_SUBST(DATE)

# -----------------------------------------------------------------------------
# Checks 
# -----------------------------------------------------------------------------

AC_PROG_CC

# Don't just call it CFLAGS, since it then gets inherited into intl/, and GNU
# and I don't exactly code alike. Don't include CFLAGS as set by AC_PROG_CC
# above, since it includes -g -O2 for gcc, for reasons unknown.
GENTOO_CFLAGS="-Wall -Wmissing-declarations -Wmissing-prototypes -Wstrict-prototypes -Werror-implicit-function-declaration"

AC_ARG_ENABLE(debug,
  [  --enable-debug          have debug info compiled in],
  [ if test "x$enableval" = "xyes"; then
      AC_DEFINE(DEBUG, 1, [Whether debug code should be compiled in.])
      GENTOO_CFLAGS="$GENTOO_CFLAGS -g -pg"
    else
      AC_DEFINE(DEBUG, 0, [Whether debug code should be compiled in.])
      GENTOO_CFLAGS="$GENTOO_CFLAGS -O2"
    fi])

AC_SUBST(GENTOO_CFLAGS)

AC_PROG_CPP
AC_PROG_RANLIB
AM_PATH_GTK_3_0(3.12.0,,AC_MSG_ERROR(Bad GTK+ version))

PKG_PROG_PKG_CONFIG()

PKG_CHECK_MODULES(MODULES, gio-2.0 gthread-2.0, dummy=yes, AC_MSG_ERROR(GIO and gthread are both required))
AC_SUBST(MODULES_CFLAGS)
AC_SUBST(MODULES_LIBS)


# Let's try to test if the host supports "file -f - -n", i.e. if the file
# command can read file names from stdin (-f -) and also flush (-n) its
# answers as soon as possible. This is a highly desirable combination of
# capabilities from gentoo's point of view. I encourage everyone to upgrade
# their file commands if they fail this test.
AC_MSG_CHECKING([whether file understands -f - -n option combo])
if ( echo "." | file -f - -n >/dev/null 2>/dev/null ) ; then
  AC_DEFINE(HAVE_GOOD_FILE, 1, [Whether the file command supports -f - -n options.])
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
  AC_MSG_WARN(["
   This system's 'file' command (`which file`) doesn't seem to"
   support the '-f - -n' option combination. gentoo's 'file' usage"
   will be limited. Consider upgrading to the version of 'file'"
   from <ftp://ftp.astron.com/pub/file/>, version 3.35 or later."
   At the moment, you will not lose any crucial functionality by"
   running gentoo with a different 'file' command, although this"
   may change in the future."])
fi


# For my next trick, let's see if _FILE_OFFSET_BITS can be played with for large file support.
# We do this in two steps: first figure out struct stat's st_size member's size for a "plain
# compile, and it that's just 32 we try again with _FILE_OFFSET_BITS=64 defined. If that works,
# we simply build gentoo with it and hope for the best.
AC_CHECK_SIZEOF([stbuf.st_size],,[AC_INCLUDES_DEFAULT
#include <limits.h>
struct stat stbuf;])
AS_IF([test "$ac_cv_sizeof_stbuf_st_size" = 4],[
  oldflags=$CPPFLAGS
  CPPFLAGS="$CPPFLAGS -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE"
  AC_CHECK_SIZEOF([stbuf64.st_size],,[AC_INCLUDES_DEFAULT
#include <limits.h>
struct stat stbuf64;])
  AS_IF([test "$ac_cv_sizeof_stbuf64_st_size" = 8],,CPPFLAGS=$oldflags)
])

# Check if the math library needs to be linked to explicitly in order to
# get the pow() function.
AC_CHECK_LIB([m], [pow])



# Kick the gettext subsystem into action.
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19.7])

# -----------------------------------------------------------------------------
# Output
# -----------------------------------------------------------------------------

AM_CONFIG_HEADER(config.h)
AC_OUTPUT([gentoorc gentoogtkrc gentoo.spec Makefile icons/Makefile\
	   src/Makefile po/Makefile.in m4/Makefile])
