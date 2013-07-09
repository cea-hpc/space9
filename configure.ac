AC_INIT([libspace9], [0.1], [dominique.martinet@cea.fr])
AM_INIT_AUTOMAKE([foreign])

AC_CONFIG_MACRO_DIR([m4])

AC_GNU_SOURCE
AC_PROG_CC
AC_PROG_LIBTOOL

AC_DEFINE( [VERSION_COMMENT], ["9p/rdma userspace library"], [No Comment])

# Git latest commit
AC_MSG_CHECKING( [Git HEAD] )
head_commit=`git rev-parse HEAD 2>/dev/null`

if test "x$head_commit" == "x" ; then
  AC_MSG_RESULT( [no git here] ) 
  AC_DEFINE_UNQUOTED( [_GIT_HEAD_COMMIT], "not compiled within a git repository", [Lastest HEAD at the time configure was run])
  AC_MSG_CHECKING( [Git describe] )
  AC_DEFINE_UNQUOTED( [_GIT_DESCRIBE], "not compiled within a git repository", [Result of git-describe --long])
  AC_SUBST(_GIT_HEAD_TAG, [${VERSION}] )
  AC_SUBST(_GIT_HEAD_DESCRIBE)
else
  AC_MSG_RESULT( $head_commit ) 
  AC_DEFINE_UNQUOTED( [_GIT_HEAD_COMMIT], "$head_commit", [Lastest HEAD at the time configure was run])
  AC_MSG_CHECKING( [Git describe] )
  git_describe=`git describe --long`
  AC_MSG_RESULT( $git_describe ) 
  AC_DEFINE_UNQUOTED( [_GIT_DESCRIBE], "$git_describe", [Result of git-describe --long])
  git_tag=${git_describe%%-*}
  git_describe=${git_describe#*-}
  if test "x$git_describe" != "x"; then
    AC_SUBST(_GIT_HEAD_TAG, [${git_tag}] )
    AC_SUBST(_GIT_HEAD_DESCRIBE, [${git_describe//-/.}] )
  else
    AC_SUBST(_GIT_HEAD_TAG, [${VERSION}] )
    AC_SUBST(_GIT_HEAD_DESCRIBE, [1.g${head_commit:0:7}] )
  fi
fi

# uid override
AC_ARG_ENABLE(uid-override,
	AC_HELP_STRING([--enable-uid-override], [Allow user to specify uid in config file (disabled)]))

if test "x$enable_uid_override" == "xyes" ; then
	AC_DEFINE([ALLOW_UID_OVERRIDE], [1], [Defined to allow UID override])
fi

# readline for shell
AC_ARG_ENABLE(readline,
	AC_HELP_STRING([--enable-readline], [Use readline for 9p_shell (auto)]))

if test "x$enable_readline" != "xno" ; then
	AC_CHECK_LIB([readline], [readline],
		[have_readline=1; AC_DEFINE([HAVE_READLINE], [1], [Defined to use readline])],
		[if test "x$enable_readline" == "xyes"; then AC_MSG_ERROR([unable to find readline]); fi])
fi
AM_CONDITIONAL(ENABLE_READLINE, test "x$have_readline" == "x1")


#don't add -lmooshika if tcp emulation is used
AC_ARG_WITH(mooshika,
	AC_HELP_STRING([--without-mooshika], [don't use mooshika]))

if test "x$with_mooshika" != "xno" ; then
	# Check for mooshika lib and header
	if test "x$with_mooshika" != "x" ; then
		LIBS="-L$with_mooshika/lib $LIBS"
		CFLAGS="-I$with_mooshika/include $CFLAGS"
	fi
	AC_CHECK_HEADERS([mooshika.h],[], [AC_MSG_ERROR([missing mooshika headers, if on purpose use --without-mooshika])])
	AC_SEARCH_LIBS([msk_init], [mooshika], [enable_moosika=yes; AC_DEFINE([HAVE_MOOSHIKA], [1], [Defined to use mooshika])], [
		AC_MSG_ERROR([unable to find mooshika, if on purpose use --without-mooshika])
	])
fi

AM_CONDITIONAL(HAVE_MOOSHIKA, test "x$enable_mooshika" == "xyes")

dnl =================
dnl Checks for Python
dnl =================
AM_PATH_PYTHON([],
               [has_python=yes])

if test "x$has_python" == "xyes"; then
  AC_CACHE_CHECK([for $am_display_PYTHON includes directory],
      [adl_cv_python_inc],
      [adl_cv_python_inc=`$PYTHON -c "from distutils import sysconfig; print sysconfig.get_python_inc()" 2>/dev/null`])
  AC_SUBST([PYTHON_CPPFLAGS], [-I$adl_cv_python_inc])
  AC_CHECK_HEADERS("$adl_cv_python_inc/Python.h", , [has_python=no])
fi

AX_PKG_SWIG

AC_ARG_ENABLE([python-bindings],
              AS_HELP_STRING([--enable-python-binding],
                             [build python binding (default=auto)]))

if test "x$has_python" == "xyes" && test "x$SWIG" != "x"; then
  if test "x$enable_python_binding" == "x"; then
    enable_python_binding=yes
  fi
else
  if test "x$enable_python_binding" == "xyes"; then
    AC_MSG_ERROR([Need python-devel and swig for python-bindings])
  fi
fi
AM_CONDITIONAL(ENABLE_PYTHON_BINDING, test "x$enable_python_binding" == "xyes")

AM_CONDITIONAL(HAVE_SWIG, test "x$SWIG" != "x")

# for exporting to spec file
AC_SUBST(ac_configure_args)

AC_CONFIG_HEADERS([include/config.h])
AC_CONFIG_FILES([Makefile src/Makefile src/tests/Makefile src/libspace9.pc libspace9.spec src/python/setup.py src/python/Makefile])
AC_OUTPUT
