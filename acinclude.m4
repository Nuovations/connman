AC_DEFUN([AC_PROG_CC_PIE], [
	AC_CACHE_CHECK([whether ${CC-cc} accepts -fPIE], ac_cv_prog_cc_pie, [
		echo 'void f(){}' > conftest.c
		if test -z "`${CC-cc} -fPIE -pie -c conftest.c 2>&1`"; then
			ac_cv_prog_cc_pie=yes
		else
			ac_cv_prog_cc_pie=no
		fi
		rm -rf conftest*
	])
])

AC_DEFUN([AC_PROG_CC_ASAN], [
	AC_CACHE_CHECK([whether ${CC-cc} accepts -fsanitize=address], ac_cv_prog_cc_asan, [
		echo 'void f(){}' > conftest.c
		if test -z "`${CC-cc} -fsanitize=address -c conftest.c 2>&1`"; then
			ac_cv_prog_cc_asan=yes
		else
			ac_cv_prog_cc_asan=no
		fi
		rm -rf conftest*
	])
])

AC_DEFUN([AC_PROG_CC_LSAN], [
	AC_CACHE_CHECK([whether ${CC-cc} accepts -fsanitize=leak], ac_cv_prog_cc_lsan, [
		echo 'void f(){}' > conftest.c
		if test -z "`${CC-cc} -fsanitize=leak -c conftest.c 2>&1`"; then
			ac_cv_prog_cc_lsan=yes
		else
			ac_cv_prog_cc_lsan=no
		fi
		rm -rf conftest*
	])
])

AC_DEFUN([AC_PROG_CC_UBSAN], [
	AC_CACHE_CHECK([whether ${CC-cc} accepts -fsanitize=undefined], ac_cv_prog_cc_ubsan, [
		echo 'void f(){}' > conftest.c
		if test -z "`${CC-cc} -fsanitize=undefined -c conftest.c 2>&1`"; then
			ac_cv_prog_cc_ubsan=yes
		else
			ac_cv_prog_cc_ubsan=no
		fi
		rm -rf conftest*
	])
])

AC_DEFUN([COMPILER_FLAGS], [
	if (test "${CFLAGS}" = ""); then
		CFLAGS="-Wall -fsigned-char -fno-exceptions"
	fi
	if (test "$USE_MAINTAINER_MODE" = "yes"); then
		CFLAGS+=" -Werror -Wextra"
		CFLAGS+=" -Wno-unused-parameter"
		CFLAGS+=" -Wno-missing-field-initializers"
		CFLAGS+=" -Wdeclaration-after-statement"
		CFLAGS+=" -Wmissing-declarations"
		CFLAGS+=" -Wredundant-decls"
		CFLAGS+=" -DG_DISABLE_DEPRECATED"
	fi

	if (test "$CC" = "clang"); then
		CFLAGS+=" -Wno-unknown-warning-option"
		CFLAGS+=" -Wno-unknown-pragmas"
	fi

	if (test "$CC" = "gcc"); then
		CFLAGS="$CFLAGS -Wcast-align"
	fi

])

# configmake.m4 serial 2
dnl Copyright (C) 2010-2015 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

# gl_CONFIGMAKE_PREP
# ------------------
AC_DEFUN([gl_CONFIGMAKE_PREP],
[
  dnl Added in autoconf 2.70
  if test "x$runstatedir" = x; then
    AC_SUBST([runstatedir], ['${localstatedir}/run'])
  fi
])
