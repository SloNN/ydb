# Generated by devtools/yamaker.

PROGRAM(acountry)

WITHOUT_LICENSE_TEXTS()

OWNER(
    max42
    g:cpp-contrib
)

LICENSE(MIT)

PEERDIR(
    contrib/libs/c-ares
)

ADDINCL(
    contrib/libs/c-ares/include
    contrib/libs/c-ares/src/lib
)

NO_COMPILER_WARNINGS()

CFLAGS(
    -DHAVE_CONFIG_H
)

SRCDIR(contrib/libs/c-ares/src)

IF (NOT DLL_FOR)
    CFLAGS(
        -DCARES_STATICLIB
    )
ENDIF()

SRCS(
    tools/acountry.c
)

END()
