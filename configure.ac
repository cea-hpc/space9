AC_INIT([9p], [0.1], [dominique.martinet@cea.fr])
AM_INIT_AUTOMAKE([foreign])

AC_CONFIG_MACRO_DIR([m4])

AC_GNU_SOURCE
AC_PROG_CC
AC_PROG_LIBTOOL

# Check for mooshika lib and header
AC_SEARCH_LIBS([msk_init], [mooshika], [], [
	AC_MSG_ERROR([unable to find mooshika])
])
AC_CHECK_HEADERS([mooshika.h],[], [AC_MSG_ERROR(missing mooshika headers)])


AC_DEFINE( [VERSION_COMMENT], ["9p/rdma userspace library"], [No Comment])

# Git latest commit
AC_MSG_CHECKING( [Git HEAD] )
head_commit=`git rev-parse HEAD 2>/dev/null`

if test "x$head_commit" == "x" ; then
  AC_MSG_RESULT( [no git here] ) 
  AC_DEFINE_UNQUOTED( [_GIT_HEAD_COMMIT], "not compiled within a git repository", [Lastest HEAD at the time configure was run])
  AC_MSG_CHECKING( [Git describe] )
  AC_DEFINE_UNQUOTED( [_GIT_DESCRIBE], "not compiled within a git repository", [Result of git-describe --long])
  AC_SUBST(_GIT_HEAD_COMMIT_ABBREV)
else
  AC_MSG_RESULT( $head_commit ) 
  AC_DEFINE_UNQUOTED( [_GIT_HEAD_COMMIT], "$head_commit", [Lastest HEAD at the time configure was run])
  AC_MSG_CHECKING( [Git describe] )
  git_describe=`git describe --long`
  AC_MSG_RESULT( $git_describe ) 
  AC_DEFINE_UNQUOTED( [_GIT_DESCRIBE], "$git_describe", [Result of git-describe --long])
  AC_SUBST(_GIT_HEAD_COMMIT_ABBREV, .${head_commit:0:7})
fi


#for exporting to spec file
AC_SUBST(ac_configure_args)

AC_CONFIG_HEADERS([include/config.h])
AC_CONFIG_FILES([Makefile src/Makefile src/tests/Makefile src/lib9p.pc lib9p.spec])
AC_OUTPUT
