AC_INIT([tyr],
	[0.0.1],
	[https://github.com/valhalla/tyr/issues],
	[tyr-0.0.1],
	[https://github.com/valhalla/tyr])
AM_INIT_AUTOMAKE([subdir-objects parallel-tests])
LT_INIT
AC_SUBST([LIBTOOL_DEPS])

AM_SILENT_RULES([yes])
AC_CONFIG_HEADERS([valhalla/config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_HEADER_STDC
AC_LANG_CPLUSPLUS

# require c++11
AX_CXX_COMPILE_STDCXX_11([noext],[mandatory])

# require other valhalla dependencies
CHECK_VALHALLA_MIDGARD
CHECK_VALHALLA_BALDR
CHECK_VALHALLA_SIF
CHECK_VALHALLA_MJOLNIR
CHECK_VALHALLA_LOKI
CHECK_VALHALLA_ODIN
CHECK_VALHALLA_THOR

AX_PYTHON_DEVEL([>= 2.6])
AM_PATH_PYTHON([2.6],,)

# check for boost and make sure we have the program options library
AX_BOOST_BASE([1.54], , [AC_MSG_ERROR([cannot find Boost libraries, which are are required for building tyr. Please install libboost-dev.])])
AX_BOOST_PYTHON

# check for Lua libraries and headers
AX_PROG_LUA([5.2],[],[
    AX_LUA_HEADERS([
        AX_LUA_LIBS([
        ],[AC_MSG_ERROR([Cannot find Lua libs.   Please install lua5.2 liblua5.2-dev])])
    ],[AC_MSG_ERROR([Cannot find Lua includes.  Please install lua5.2 liblua5.2-dev])])
],[AC_MSG_ERROR([Cannot find Lua interpreter.   Please install lua5.2 liblua5.2-dev])])

# optionally enable coverage information
CHECK_COVERAGE

AC_CONFIG_FILES([Makefile])

# Debian resets this to no, but this break both Spot and the libtool
# test suite itself.  Instead of requiring developer to install a
# non-patched version of Libtool on any Debian they use, we just
# cancel the effect of Debian's patch here.
# see: http://git.lrde.epita.fr/?p=spot.git;a=commitdiff;h=0e74b76521341f670f6b76f8ef24a6dcf6e3813b
link_all_deplibs=yes
link_all_deplibs_CXX=yes

AC_OUTPUT

