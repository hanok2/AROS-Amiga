/*
    Copyright (C) 1995-2001 AROS
    $Id$

    Desc: Internal definitions for the locale.library.
    Lang: english
*/


#ifndef EXEC_EXECBASE_H
#include <exec/execbase.h>
#endif
#ifndef EXEC_SEMAPHORES_H
#include <exec/semaphores.h>
#endif
#ifndef DOS_DOSEXTENS_H
#include <dos/dosextens.h>
#endif
#ifndef LIBRARIES_LOCALE_H
#include <libraries/locale.h>
#endif

/* aros_print_not_implemented() macro: */
#include <aros/debug.h>
#include <aros/asmcall.h>
#include <aros/libcall.h>

/* Should the Loc???() replacement functions lock the default locale.
   1 = yes. 0 = no. If you set this to 0, then you must make sure that
   a Locale which was once set as default Locale never gets freed from
   memory. */
   
#define REPLACEMENTFUNCS_LOCK_LOCALE 0

struct IntLocaleBase
{
    struct LocaleBase        lb_LocaleBase;
    struct ExecBase         *lb_SysBase;
    struct DosLibrary       *lb_DosBase;
    struct Library          *lb_IFFParseBase;
    struct Library          *lb_UtilityBase;

    struct IntLocale	    *lb_CurrentLocale;
    struct SignalSemaphore   lb_LocaleLock;
    struct SignalSemaphore   lb_CatalogLock;
    struct MinList           lb_CatalogList;
};

struct IntLocale
{
    struct Locale       il_Locale;

    UWORD                il_Count;
    struct Library      *il_CurrentLanguage;
    APTR                 il_LanguageFunctions[32];

    /* Need to put all sorts of crap here later. */
    UBYTE   	    	LocaleName[30];
    UBYTE   	    	LanguageName[30];
    
    UBYTE		PreferredLanguages[10][30];

    /*
    ** Don't change the order of the following field,
    ** It's based on CountryPrefs structure in
    ** <prefs/locale.h> and InitLocale() relies on this
    ** as well!
    */
      
    UBYTE		DateTimeFormat[80];
    UBYTE		DateFormat[40];
    UBYTE		TimeFormat[40];
    UBYTE		ShortDateTimeFormat[80];
    UBYTE		ShortDateFormat[40];
    UBYTE		ShortTimeFormat[40];

    UBYTE		DecimalPoint[10];
    UBYTE		GroupSeparator[10];
    UBYTE		FracGroupSeparator[10];
    UBYTE		Grouping[10];
    UBYTE		FracGrouping[10];
    UBYTE		MonDecimalPoint[10];
    UBYTE		MonGroupSeparator[10];
    UBYTE		MonFracGroupSeparator[10];
    UBYTE		MonGrouping[10];
    UBYTE		MonFracGrouping[10];

    UBYTE		MonCS[10];
    UBYTE		MonSmallCS[10];
    UBYTE		MonIntCS[10];
    UBYTE		MonPositiveSign[10];

    UBYTE		MonNegativeSign[10];
};

struct CatStr
{
    STRPTR  	    	cs_String;
    ULONG               cs_Id;
};

/* see Amiga Developer CD 2.1:NDK/NDK_3.1/Examples1/locale/SelfLoad/catalog.c */

struct CodeSet
{
    ULONG cs_CodeSet;
    ULONG cs_Reserved[7];
};

struct IntCatalog
{
    struct Catalog      ic_Catalog;
    struct CodeSet  	ic_CodeSet;
    struct CatStr      *ic_CatStrings;
    UBYTE   	       *ic_StringChunk;
    ULONG   	    	ic_NumStrings;
    ULONG		ic_DataSize;
    UWORD		ic_UseCount;
    ULONG		ic_Flags;
    UBYTE   	    	ic_LanguageName[30];
    UBYTE   	    	ic_Name[0]; // name of the file as passed to OpenCatalogA
    /* structure sizes depends on length of ic_Name string */
};

/* Catalog strings are in order, so we don't have to search them all */
#define ICF_INORDER	(1L<<0)

/* Shortcuts to the internal structures */
#define IntLB(lb)      ((struct IntLocaleBase *)(lb))
#define IntL(locale)   ((struct IntLocale *)(locale))
#define IntCat(cat)    ((struct IntCatalog *)(cat))

/* Global Library bases in our library base */
#define SysBase     (((struct IntLocaleBase *)LocaleBase)->lb_SysBase)
#define DOSBase     (((struct IntLocaleBase *)LocaleBase)->lb_DosBase)
#define IFFParseBase (((struct IntLocaleBase *)LocaleBase)->lb_IFFParseBase)
#define UtilityBase (((struct IntLocaleBase *)LocaleBase)->lb_UtilityBase)

#if REPLACEMENTFUNCS_LOCK_LOCALE
#define REPLACEMENT_LOCK    ObtainSemaphore(&IntLB(LocaleBase)->lb_LocaleLock)
#define REPLACEMENT_UNLOCK  ReleaseSemaphore(&IntLB(LocaleBase)->lb_LocaleLock)
#else
#define REPLACEMENT_LOCK
#define REPLACEMENT_UNLOCK
#endif

#define ID_CTLG MAKE_ID('C','T','L','G')
#define ID_FVER MAKE_ID('F','V','E','R')
#define ID_LANG MAKE_ID('L','A','N','G')
#define ID_CSET MAKE_ID('C','S','E','T')
#define ID_STRS MAKE_ID('S','T','R','S')


void dispose_catalog(struct IntCatalog * cat,
                     struct LocaleBase * LocaleBase);

void InstallPatches(void);
