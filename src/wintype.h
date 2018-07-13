#pragma once


#ifdef WIN32
    #include <basetsd.h>

/*
	#ifndef sszie_t
		typedef SSIZE_T ssize_t;
	#endif
	*/
#ifdef _MSC_VER
#if !defined(ssize_t)
#ifdef _WIN64
typedef int64_t ssize_t;
#else
typedef int32_t ssize_t;
#endif
#endif
#endif

	#undef HAVE_BYTESWAP_H
	#undef HAVE_ENDIAN_H

	/* Define to 1 if you have the declaration of `be16toh', and to 0 if you
	don't. */
#define HAVE_DECL_BE16TOH 0

	/* Define to 1 if you have the declaration of `be32toh', and to 0 if you
	don't. */
#define HAVE_DECL_BE32TOH 0

	/* Define to 1 if you have the declaration of `be64toh', and to 0 if you
	don't. */
#define HAVE_DECL_BE64TOH 0

	/* Define to 1 if you have the declaration of `bswap_16', and to 0 if you
	don't. */
#define HAVE_DECL_BSWAP_16 0

	/* Define to 1 if you have the declaration of `bswap_32', and to 0 if you
	don't. */
#define HAVE_DECL_BSWAP_32 0

	/* Define to 1 if you have the declaration of `bswap_64', and to 0 if you
	don't. */
#define HAVE_DECL_BSWAP_64 0

	/* Define to 1 if you have the declaration of `daemon', and to 0 if you don't.
	*/
#define HAVE_DECL_DAEMON 0

	/* Define to 1 if you have the declaration of `EVP_MD_CTX_new', and to 0 if
	you don't. */
#define HAVE_DECL_EVP_MD_CTX_NEW 0

	/* Define to 1 if you have the declaration of `htobe16', and to 0 if you
	don't. */
#define HAVE_DECL_HTOBE16 0

	/* Define to 1 if you have the declaration of `htobe32', and to 0 if you
	don't. */
#define HAVE_DECL_HTOBE32 0

	/* Define to 1 if you have the declaration of `htobe64', and to 0 if you
	don't. */
#define HAVE_DECL_HTOBE64 0

	/* Define to 1 if you have the declaration of `htole16', and to 0 if you
	don't. */
#define HAVE_DECL_HTOLE16 0

	/* Define to 1 if you have the declaration of `htole32', and to 0 if you
	don't. */
#define HAVE_DECL_HTOLE32 0

	/* Define to 1 if you have the declaration of `htole64', and to 0 if you
	don't. */
#define HAVE_DECL_HTOLE64 0

	/* Define to 1 if you have the declaration of `le16toh', and to 0 if you
	don't. */
#define HAVE_DECL_LE16TOH 0

	/* Define to 1 if you have the declaration of `le32toh', and to 0 if you
	don't. */
#define HAVE_DECL_LE32TOH 0

	/* Define to 1 if you have the declaration of `le64toh', and to 0 if you
	don't. */
#define HAVE_DECL_LE64TOH 0

	/* Define to 1 if you have the declaration of `strerror_r', and to 0 if you
	don't. */
#define HAVE_DECL_STRERROR_R 0

	#undef HAVE_DLFCN_H

	#undef HAVE_ENDIAN_H
	#undef HAVE_MSG_NOSIGNAL
	#undef HAVE_STRERROR_R
	#undef HAVE_SYS_ENDIAN_H
	#undef HAVE_SYS_PRCTL_H
	#undef HAVE_SYS_SELECT_H
	#undef HAVE_WORKING_BOOST_SLEEP
	#undef PTHREAD_CREATE_JOINABLE
	#undef QT_QPA_PLATFORM_COCOA
	#undef QT_QPA_PLATFORM_XCB
	#undef STRERROR_R_CHAR_P
	#undef USE_DBUS

	#if defined AC_APPLE_UNIVERSAL_BUILD
	# if defined __BIG_ENDIAN__
	#  define WORDS_BIGENDIAN 1
	# endif
	#else
	# ifndef WORDS_BIGENDIAN
		/* #  undef WORDS_BIGENDIAN */
	# endif
	#endif

	#undef _LARGE_FILES

	#undef HAVE_DECL___BUILTIN_CLZL
	#undef HAVE_DECL___BUILTIN_CLZLL
	#undef HAVE_SYS_GETRANDOM
	#undef HAVE_MSG_DONTWAIT
	#undef HAVE_MALLOPT_ARENA_MAX
	#undef HAVE_MALLOC_INFO
#endif
