#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

/*
    (C) 1995-96 AROS - The Amiga Replacement OS
    $Id$

    Desc: ANSI-C header file sys/types.h
    Lang: english
*/

#if !defined(_SIZE_T) && !defined(__typedef_size_t)
#define __typedef_size_t
#define _SIZE_T
/* Must be int and not long. Otherwise gcc will complain */
typedef unsigned int size_t;
#endif

#if !defined(_SSIZE_T) && !defined(__typedef_ssize_t)
#define __typedef_ssize_t
#define _SSIZE_T
typedef int ssize_t;
#endif

#endif /* _SYS_TYPES_H */
