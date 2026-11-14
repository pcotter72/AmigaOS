/*******************************************************************************

   NAME

	RACHOICE

   FORMAT

	RACHOICE <title> <body> <gadgets> [PUBSCREEN] [SET] [SETENV] [IMAGE] [FILE] [QUIET] [DEBUG]

   TEMPLATE

	TITLE/A,BODY/A,GADGETS/A/M,PUBSCREEN/K,SET/K,SETENV/K,IMAGE/K,FILE/S,QUIET/S,DEBUG/S

   PATH

	C:RACHOICE

   FUNCTION

	Allows AmigaDOS scripts to use Reaction-based message requesters.

	TITLE       - The text to display in the title bar of the requester.
	BODY        - The text to display in the body of the requester.
	GADGETS     - The text for each of the buttons.
	PUBSCREEN   - The name of the public screen to open the requester upon.
	SET         - The LOCAL environment variable where the result is written to.
	SETENV      - The GLOBAL environment variable where the result is written to.
	FILE        - The BODY argument is a filename. Its content will be used.
	IMAGE       - The pictogram or the image path to display, among :
			INFO       - Display a INFO pictogram.
			WARNING    - Display a WARNING pictogram.
			ERROR      - Display a ERROR pictogram.
			QUESTION   - Display a QUESTION pictogram.
			INSERTDISK - Display a INSERTDISK pictogram.
        		ELSE load any supported datatype image from disk.
	QUIET       - Suppress stdout messages.
	DEBUG       - Outputs debug information.

   RESULT

	Standard DOS return codes.

   NOTES

	This program use the V47 'classes/requester.class',
	and, if asked, the 'images/bitmap.image' system objects.

	To place a newline into the BODY of the requester use *n or *N.

	To place a quotation mark in the BODY of the requester use *".

   EXAMPLES

	RAChoice Info Line Ok

	RAChoice "Info" "line1*nline2" "ok|cancel"
	RAChoice "Warn" "line1*nline2" "ok|cancel" IMAGE=WARNING
	RAChoice "Warn" "line1*nline2" "ok|cancel" IMAGE=WARNING SET=MYVAR
	RAChoice "Warn" "line1*nline2" "ok|cancel" IMAGE=RAM:WARN.PNG
	RAChoice "Warn" "line1*nline2" "ok|cancel" IMAGE=RAM:WARN.info

	C:Status >T:0

	RAChoice Status T:0 Ok FILE

   CAOSWIFI / CAFFEINEOS CHANGES (1.3, 31.10.2026, Pedro Cotter; MPL-2.0 like
   the original - upstream: github.com/flype44/AmigaOS, RAChoice 1.2, commit
   0d782f7)

	- The parent window for requester.class (RM_OPENREQ or_Window) was the
	  process's pr_WindowPtr as it is.  pr_WindowPtr = (APTR)-1 means "no
	  DOS requesters" and is inherited by every process started below a
	  program that sets it (amiagent does; measured: a Run from it gives
	  $FFFFFFFF).  requester.class then used -1 as a window and the
	  requester never opened: RAChoice hung for good in a semaphore wait
	  inside requester.class 47.11 (A2000, OS 3.2.3, two TaskDumps).
	  -1 is now passed as NULL (no reference window), which the
	  requester.class autodoc example uses.
	- Without a reference window a screen is always passed (PUBSCREEN, else
	  the default public screen): requester.class before V47 requires one
	  ("REQUIRED if or_Window is not supplied", NDK 3.9 requester.h) - OS
	  3.9 has requester.class 42.
	- PUBSCREEN is honoured: its screen becomes or_Screen (it was read but
	  never used).
	- FreeArgs() after use (the ReadArgs() result was never freed).
	- Built with gcc (m68k-amigaos-gcc 6.5.0b, libnix) instead of SAS/C:
	  proto/ headers, the tag list for RM_OPENREQ built as an array (the
	  old varargs helper passed &Tag1, which gcc's inlining breaks), and
	  utility.library Stricmp() instead of stricmp().

********************************************************************************/

#define USE_BUILTIN_MATH
#define USE_SYSBASE

#include <stdio.h>
#include <string.h>
#include <math.h>

#define	INTUI_V36_NAMES_ONLY

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <utility/tagitem.h>

#include <clib/alib_protos.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/datatypes.h>
#include <proto/requester.h>
#include <proto/bitmap.h>

#define ALL_REACTION_CLASSES
#define ALL_REACTION_MACROS

#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>

#include <images/bitmap.h>

#include "ownreq.h"         /* 1.5: icon loader and own requester window */
#include <classes/requester.h>
#include <classes/window.h>

/*****************************************************************************/

#define TEMPLATE "TITLE/A,BODY/A,GADGETS/A,PUBSCREEN/K,SET/K,SETENV/K,FILE/S,IMAGE/K,QUIET/S,DEBUG/S"

/*****************************************************************************/

enum {
    OPT_TITLE,       /* Compatible with CBM's RequestChoice */
    OPT_BODY,        /* Compatible with CBM's RequestChoice */
    OPT_GADGETS,     /* Compatible with CBM's RequestChoice */
    OPT_PUBSCREEN,   /* Compatible with CBM's RequestChoice */
    OPT_SET,         /* Compatible with CBM's RequestChoice */
    OPT_SETENV,      /* NEW: Set Global env variable        */
    OPT_FILE,        /* NEW: Load Body from file            */
    OPT_IMAGE,       /* NEW: Feature from requester.class   */
    OPT_QUIET,       /* NEW: Suppress stdout messages       */
    OPT_DEBUG,       /* NEW: Outputs picture information    */
    OPT_COUNT        /* Number of supported arguments       */
};

/*****************************************************************************/

static const char VersionTag[] __attribute__((used)) =
    "$VER: RAChoice 1.5 (13.11.2026) Philippe CARPENTIER, CaffeineOS build";

/*****************************************************************************/

struct Library *BitMapBase = NULL;      /* opened in GetReqImage() */
struct Library *RequesterBase = NULL;   /* opened in main() */
struct Image   *RequesterImage;

BOOL bDEBUG = FALSE;
BOOL bQUIET = FALSE;

/*****************************************************************************/

Object * LoadPicture(STRPTR name)
{
    Object * picture = NULL;
    struct Screen * screen = NULL;

    if (screen = LockPubScreen(NULL))
    {
        picture = NewDTObject(
            name,
            DTA_GroupID,           GID_PICTURE,
            DTA_SourceType,        DTST_FILE,
            PDTA_DestMode,         PMODE_V43,
            PDTA_FreeSourceBitMap, TRUE,
            PDTA_Remap,            TRUE,
            PDTA_Screen,           screen,
            OBP_Precision,         PRECISION_EXACT,
            OBP_FailIfBad,         FALSE,
            TAG_DONE);

        if (picture)
        {
            DoDTMethod(picture, NULL, NULL, DTM_PROCLAYOUT, NULL, TRUE);
        }

        UnlockPubScreen(NULL, screen);
    }

    return (picture);
}

/*****************************************************************************/

ULONG OpenRequesterTagList(Object *obj, struct TagItem *tags, struct Screen *screen)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    APTR window = process->pr_WindowPtr;

    struct orRequest msg[1];

    /* 1.3: pr_WindowPtr (APTR)-1 = "no DOS requesters", not a window: it is
     * inherited from daemons such as amiagent, and requester.class took it as
     * one and hung.  NULL = no reference window (requester.class autodoc). */
    struct Screen *defScreen = NULL;
    ULONG result;

    if (window == (APTR)-1)
    {
        window = NULL;
    }

    /* 1.3: without a reference window requester.class needs a screen before
     * V47: "or_Screen ... REQUIRED if or_Window is not supplied" (NDK 3.9
     * classes/requester.h; NDK 3.2: "Until V47, this was required").  So the
     * PUBSCREEN screen, else the default public screen, locked until the
     * requester has closed. */
    if (window == NULL && screen == NULL)
    {
        screen = defScreen = LockPubScreen(NULL);
    }

    msg->MethodID = RM_OPENREQ;
    msg->or_Window = (struct Window *)window;
    msg->or_Screen = screen;
    msg->or_Attrs = tags;

    result = DoMethodA(obj, (Msg)msg);

    if (defScreen)
    {
        UnlockPubScreen(NULL, defScreen);
    }

    return(result);
}

/*****************************************************************************/

ULONG GetReqImage(LONG opts[OPT_COUNT])
{
    STRPTR s;

    if (s = (STRPTR)opts[OPT_IMAGE])
    {
        if (!Stricmp(s, (STRPTR)"INFO"      )) return(REQIMAGE_INFO      );
        if (!Stricmp(s, (STRPTR)"WARNING"   )) return(REQIMAGE_WARNING   );
        if (!Stricmp(s, (STRPTR)"ERROR"     )) return(REQIMAGE_ERROR     );
        if (!Stricmp(s, (STRPTR)"QUESTION"  )) return(REQIMAGE_QUESTION  );
        if (!Stricmp(s, (STRPTR)"INSERTDISK")) return(REQIMAGE_INSERTDISK);

        if (BitMapBase = OpenLibrary("images/bitmap.image", 0L))
        {
            struct Screen *screen;

            if(screen = LockPubScreen(NULL))
            {
                Object * picture;

                /*
                struct Image *RequesterImageAlt;
                RequesterImageAlt = BitMapObject,
                    BITMAP_SourceFile, s,
                    BITMAP_Screen, screen,
                    BITMAP_Precision, PRECISION_EXACT,
                    BITMAP_Masking, TRUE,
                    BITMAP_MaskPlane, (APTR)GetReqImage,
                    BITMAP_Transparent, TRUE,
                    EndImage;
                */

                if (picture = LoadPicture(s))
                {
                    struct BitMapHeader * bitMapHeader = NULL;

                    GetDTAttrs(picture, PDTA_BitMapHeader, &bitMapHeader, TAG_DONE);

                    if (bitMapHeader)
                    {
                        APTR maskPlane = NULL;
                        struct BitMap * bitMap = NULL;

                        GetDTAttrs(picture, PDTA_BitMap, &bitMap, TAG_DONE);
                        GetDTAttrs(picture, PDTA_MaskPlane, &maskPlane, TAG_DONE);

                        if (bDEBUG)
                        {
                            printf("Picture:   0x%08lx\n", (unsigned long)picture);
                            printf("BitMap:    0x%08lx\n", (unsigned long)bitMap);
                            printf("BitMap.w:  %u\n",      bitMapHeader->bmh_Width);
                            printf("BitMap.h:  %u\n",      bitMapHeader->bmh_Height);
                            printf("MaskPlane: 0x%08lx\n", (unsigned long)maskPlane);
                        }

                        if (!maskPlane && !bQUIET)
                        {
                            printf("WARNING: the picture maskPlane is null.\n");
                        }

                        RequesterImage = BitMapObject,
                            BITMAP_Screen,      screen,
                            BITMAP_Precision,   PRECISION_IMAGE,
                            BITMAP_BitMap,      bitMap,
                            BITMAP_Masking,     ((maskPlane != NULL) ? TRUE : FALSE),
                            BITMAP_MaskPlane,   maskPlane,
                            BITMAP_Width,       bitMapHeader->bmh_Width,
                            BITMAP_Height,      bitMapHeader->bmh_Height,
                            BITMAP_Transparent, ((maskPlane != NULL) ? TRUE : FALSE),
                            EndImage;

                        /* 1.5: the same bitmap for the own requester (class below V47) */
                        OwnSetPicture(bitMap, bitMapHeader->bmh_Width, bitMapHeader->bmh_Height);
                    }
                    else
                    {
                        if (!bQUIET) printf("WARNING: the bitmap header is null.\n");
                    }
                }
                else if (OwnLoadIcon(s, screen))
                {
                    /* 1.5: not a picture for datatypes, but a Workbench icon.  For
                       requester.class V47+ it is drawn into a bitmap and passed as
                       the image; below V47 the own requester draws it itself. */
                    if (RequesterBase && RequesterBase->lib_Version >= 47)
                    {
                        LONG iconW = 0, iconH = 0;
                        struct BitMap *iconBitMap = OwnRenderIcon(screen, &iconW, &iconH);

                        if (iconBitMap)
                        {
                            RequesterImage = BitMapObject,
                                BITMAP_Screen,      screen,
                                BITMAP_BitMap,      iconBitMap,
                                BITMAP_Width,       iconW,
                                BITMAP_Height,      iconH,
                                EndImage;
                        }
                    }

                    if (bDEBUG) printf("Icon:      read with icon.library\n");
                }
                else
                {
                    if (!bQUIET) printf("WARNING: failed to load picture.\n");
                }

                UnlockPubScreen(NULL, screen);
            }
            else
            {
                if (!bQUIET) printf("WARNING: cant lock public screen.\n");
            }
        }
        else
        {
            /* 1.5: no bitmap.image - the own requester needs none for an icon */
            struct Screen *iconScreen = LockPubScreen(NULL);

            if (!(iconScreen && OwnLoadIcon(s, iconScreen)))
            {
                if (!bQUIET) printf("WARNING: can load bitmap.image.\n");
            }

            if (iconScreen) UnlockPubScreen(NULL, iconScreen);
        }
    }

    if (RequesterImage)
    {
        return((ULONG)RequesterImage);
    }

    return(REQIMAGE_DEFAULT);
}

/*****************************************************************************/

STRPTR FileToString(STRPTR fileName)
{
    BPTR   file = 0;
    BPTR   lock = 0;
    STRPTR buffer = NULL;
    struct FileInfoBlock* fib = NULL;

    if (lock = Lock(fileName, ACCESS_READ))
    {
        if (fib = (struct FileInfoBlock*)AllocMem(sizeof(struct FileInfoBlock), MEMF_ANY))
        {
            if (Examine(lock, fib))
            {
                if (buffer = (UBYTE*)AllocVec(fib->fib_Size + 1, MEMF_ANY))
                {
                    if (file = Open(fileName, MODE_OLDFILE))
                    {
                        Read(file, buffer, fib->fib_Size);
                        buffer[fib->fib_Size] = 0;
                        Close(file);
                    }
                }
            }

            FreeMem(fib, sizeof(struct FileInfoBlock));
        }

        UnLock(lock);
    }

    return(buffer);
}

/*****************************************************************************/

int main(void)
{
    LONG           args[OPT_COUNT];
    struct RDArgs *rdargs;
    LONG           failureCode;
    LONG           failureLevel;

    failureCode  = ERROR_INVALID_RESIDENT_LIBRARY;
    failureLevel = RETURN_FAIL;

    memset(args, 0, sizeof(args));

#ifdef OWNREQ_TEST
    OwnStackMark();
#endif

    if ((rdargs = ReadArgs((STRPTR)TEMPLATE, args, NULL)))
    {
        STRPTR Req_File    = NULL;
        STRPTR Req_Title   = (STRPTR)args[OPT_TITLE  ];
        STRPTR Req_Body    = (STRPTR)args[OPT_BODY   ];
        STRPTR Req_Gadgets = (STRPTR)args[OPT_GADGETS];

        bDEBUG  = (args[OPT_DEBUG ]  ? TRUE : FALSE);

        if (!bDEBUG)
        {
            bQUIET  = (args[OPT_QUIET ]  ? TRUE : FALSE);
            bQUIET |= (args[OPT_SET   ]) ? TRUE : FALSE;
            bQUIET |= (args[OPT_SETENV]) ? TRUE : FALSE;

            /* 1.4: output redirected (a script captures the answer) -> no warning or
               error text in front of the answer. */
            {
                BPTR out = Output();
                if (!out || !IsInteractive(out)) bQUIET = TRUE;
            }
	}

        if (args[OPT_FILE])
        {
            if (!(Req_File = FileToString(Req_Body)))
            {
                if (!bQUIET)
                {
                    printf("ERROR: failed to open file `%s`.\n", Req_Body);
                }
            }
        }

        if (Req_Body && Req_Gadgets)
        {
            failureCode = ERROR_NO_FREE_STORE;

            if (RequesterBase = OpenLibrary("requester.class", 0L))
            {
                Object *Req_Object = RequesterObject,
                    REQ_TitleText, Req_Title, TAG_DONE);

                if (Req_Object)
                {
                    char output[32];
                    struct Screen *pubScreen = NULL;
                    struct TagItem tags[5];

                    if (args[OPT_PUBSCREEN])
                    {
                        pubScreen = LockPubScreen((STRPTR)args[OPT_PUBSCREEN]);
                    }

                    tags[0].ti_Tag = REQ_Type;       tags[0].ti_Data = REQTYPE_INFO;
                    tags[1].ti_Tag = REQ_Image;      tags[1].ti_Data = GetReqImage(args);
                    tags[2].ti_Tag = REQ_BodyText;   tags[2].ti_Data = (ULONG)(Req_File ? Req_File : Req_Body);
                    tags[3].ti_Tag = REQ_GadgetText; tags[3].ti_Data = (ULONG)Req_Gadgets;
                    tags[4].ti_Tag = TAG_DONE;       tags[4].ti_Data = 0;

                    /* 1.5: requester.class shows a picture only from V47 on, and its
                       requester is cut off when the text is larger than the screen.
                       Below V47 the own requester is asked first; it answers -1 when
                       it is not needed (no picture and the text fits) or cannot be
                       opened, and the class's requester then comes as before. */
                    {
                        LONG own = -1;

                        if (RequesterBase->lib_Version < 47 || OwnTestMode())
                        {
                            own = OwnRequester(pubScreen, Req_Title, (Req_File ? Req_File : Req_Body),
                                Req_Gadgets, bDEBUG);
                        }

                        failureCode = (own >= 0) ? own : (LONG)OpenRequesterTagList(Req_Object, tags, pubScreen);
                    }

                    if (pubScreen)
                    {
                        UnlockPubScreen(NULL, pubScreen);
                    }

                    failureLevel = RETURN_OK;

                    sprintf(output, "%ld\n", (long)failureCode);

                    if (args[OPT_SET])
                    {
                        ULONG success = SetVar((STRPTR)args[OPT_SET],
                            (STRPTR)output, -1, LV_VAR + GVF_LOCAL_ONLY);

                        if (!success)
                        {
                            if (!bQUIET) printf("ERROR: failed to set LOCAL variable.\n");
                            failureLevel = RETURN_ERROR;
                        }
                    }
                    else if (args[OPT_SETENV])
                    {
                        ULONG success = SetVar((STRPTR)args[OPT_SETENV],
                            (STRPTR)output, -1, LV_VAR + GVF_GLOBAL_ONLY);

                        if (!success)
                        {
                            if (!bQUIET) printf("ERROR: failed to set GLOBAL variable.\n");
                            failureLevel = RETURN_ERROR;
                        }
                    }
                    else
                    {
                        PutStr((STRPTR)output);
                    }

                    DisposeObject(Req_Object);
                }

                CloseLibrary((struct Library *)RequesterBase);
            }
            else
            {
                if (!bQUIET) printf("ERROR: failed to open requester.class.\n");
                failureLevel = RETURN_FAIL;
                failureCode = ERROR_OBJECT_NOT_FOUND;
            }
        }
        else
        {
            if (!bQUIET) printf("ERROR: missing arguments.\n");
            failureLevel = RETURN_FAIL;
            failureCode = ERROR_REQUIRED_ARG_MISSING;
        }

        if (Req_File)
        {
            FreeVec(Req_File);
        }
        
        FreeArgs(rdargs);   /* 1.3: was never freed */
    }
    else
    {
        if (!bQUIET) printf("ERROR: bad arguments.\n");
        failureLevel = RETURN_FAIL;
        failureCode = IoErr();
    }

    if (RequesterImage)
    {
        DisposeObject(RequesterImage);
    }

    if (BitMapBase)
    {
        CloseLibrary(BitMapBase);
    }

    OwnCleanup();           /* 1.5: icon, its bitmap, the libraries of ownreq.c */

#ifdef OWNREQ_TEST
    OwnStackReport();
#endif

    SetIoErr(failureCode);

    if (failureLevel != RETURN_OK)
    {
        PrintFault(failureCode, NULL);
    }

    return(failureLevel);
}

/*****************************************************************************/
