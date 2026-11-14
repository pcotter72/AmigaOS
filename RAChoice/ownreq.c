/* ownreq.c - RAChoice 1.5: the icon loader and RAChoice's own requester window.
 *
 * Why (CaffeineOS, 13.11.2026): every IMAGE= that CaffeineOS scripts pass is a Workbench icon (.info).
 *  - requester.class shows a picture only from V47 on (NDK 3.2 classes/requester.h: REQ_Image "(V47)"; the NDK 3.9
 *    header has no REQ_Image).  OS 3.9 has requester.class 42: no picture, whatever RAChoice loads.
 *  - a classic .info is not a picture for datatypes: measured 13.11.2026, NewDTObject() fails on OS 3.9 (no icon
 *    datatype) and on a 3.2.3 CaffeineOS system (icon.datatype 47.3 present, IoErr 103); bitmap.image's
 *    BITMAP_SourceFile fails on both.  icon.library reads it on both.
 * So: OwnLoadIcon() gets the icon through icon.library; OwnRenderIcon() draws it into a bitmap for the V47 class;
 * OwnRequester() is a requester of RAChoice's own (Intuition + GadTools, both in every ROM since 2.0) used when the
 * class is older than V47 and there is a picture to show.  Everything here only ADDS a picture: every function
 * reports failure and the caller then does exactly what RAChoice 1.4 did.
 *
 * (c) 2026 Pedro Cotter for the CaffeineOS build; MPL-2.0 like RAChoice itself. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/imageclass.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/icon.h>
#include <stdio.h>
#include <string.h>

#include "ownreq.h"

/* Opened here by hand, so libnix opens none of them by itself. */
struct Library *IconBase = NULL;
struct Library *GadToolsBase = NULL;

static struct DiskObject *ownIcon = NULL;       /* from OwnLoadIcon() */
static LONG ownIconW = 0, ownIconH = 0;
static struct BitMap *ownRender = NULL;         /* from OwnRenderIcon(), ours to free */
static struct BitMap *ownPicture = NULL;        /* from OwnSetPicture(), NOT ours */
static LONG ownPicW = 0, ownPicH = 0;

#define MAXBUTTONS 12
#define OUTER      6        /* window border to contents */
#define PAD        6        /* inside the body box */
#define GAP        8        /* picture to text */
#define BTNGAP     4        /* between buttons, and body box to buttons */
#define HOTMARK    1        /* stands for the hot-key underscore in a button text (see OwnRequester) */

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Test builds only (build.sh test, -DOWNREQ_TEST): the variable RAChoiceTest = 1 makes the own requester come on
 * every system, = 2 also makes it show the bitmap of OwnRenderIcon() instead of drawing the icon itself. */
int OwnTestMode(void)
{
#ifdef OWNREQ_TEST
    char b[8];
    if (GetVar((CONST_STRPTR)"RAChoiceTest", (STRPTR)b, sizeof(b), 0) > 0 && b[0] >= '1' && b[0] <= '9') return b[0] - '0';
#endif
    return 0;
}

BOOL OwnHasPicture(void)
{
    return (BOOL)(ownIcon != NULL || ownPicture != NULL);
}

void OwnSetPicture(struct BitMap *bm, LONG w, LONG h)
{
    if (bm && w > 0 && h > 0) { ownPicture = bm; ownPicW = w; ownPicH = h; }
}

/* Size of the icon as icon.library will draw it without border.  V44 has a call for it; before that the classic
 * image in the gadget is all there is. */
static BOOL icon_size(struct DiskObject *d, LONG *w, LONG *h)
{
    if (IconBase->lib_Version >= 44) {
        struct Rectangle r;
        struct TagItem t[] = { { ICONDRAWA_Borderless, TRUE }, { TAG_DONE, 0 } };
        if (!GetIconRectangleA(NULL, d, NULL, &r, t)) return FALSE;
        *w = r.MaxX - r.MinX + 1;
        *h = r.MaxY - r.MinY + 1;
    } else {
        if (!(d->do_Gadget.Flags & GFLG_GADGIMAGE) || !d->do_Gadget.GadgetRender) return FALSE;
        *w = ((struct Image *)d->do_Gadget.GadgetRender)->Width;
        *h = ((struct Image *)d->do_Gadget.GadgetRender)->Height;
    }
    return (BOOL)(*w > 0 && *h > 0 && *w <= 1024 && *h <= 1024);
}

static void icon_draw(struct RastPort *rp, struct DiskObject *d, struct DrawInfo *dri, LONG x, LONG y)
{
    if (IconBase->lib_Version >= 44) {
        struct TagItem t[] = { { ICONDRAWA_DrawInfo, (ULONG)dri }, { ICONDRAWA_Frameless, TRUE },
                               { ICONDRAWA_Borderless, TRUE }, { ICONDRAWA_EraseBackground, FALSE }, { TAG_DONE, 0 } };
        DrawIconStateA(rp, d, NULL, x, y, IDS_NORMAL, t);
    } else
        DrawImage(rp, (struct Image *)d->do_Gadget.GadgetRender, x, y);
}

/* name = the IMAGE= argument.  Only a name ending in ".info" is taken for an icon. */
BOOL OwnLoadIcon(CONST_STRPTR name, struct Screen *scr)
{
    char buf[256];
    LONG n = strlen((const char *)name);

    if (ownIcon) return TRUE;
    if (n <= 5 || n >= (LONG)sizeof(buf)) return FALSE;
    if (lower(name[n - 5]) != '.' || lower(name[n - 4]) != 'i' || lower(name[n - 3]) != 'n' || lower(name[n - 2]) != 'f'
        || lower(name[n - 1]) != 'o') return FALSE;
    memcpy(buf, name, n - 5);
    buf[n - 5] = 0;
    if (!IconBase && !(IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 36))) return FALSE;
    if (!(ownIcon = GetDiskObject((CONST_STRPTR)buf))) return FALSE;
    if (IconBase->lib_Version >= 44 && scr) LayoutIconA(ownIcon, scr, NULL);   /* palette-mapped icons: our screen */
    if (!icon_size(ownIcon, &ownIconW, &ownIconH)) {
        FreeDiskObject(ownIcon);
        ownIcon = NULL;
        return FALSE;
    }
    return TRUE;
}

/* The loaded icon drawn into a bitmap of the screen's kind, on the screen's background pen, for requester.class V47
 * (REQ_Image wants an image; the caller wraps the bitmap in a bitmap.image).  The bitmap stays ours; OwnCleanup()
 * frees it.  NULL = not possible.
 * Measured on an A600 with a PiStorm (Emu68), OS 3.2.3, icon.library 51.4, 13.11.2026: drawn through a layer made
 * on the bitmap (CreateUpfrontLayer) the icon does NOT appear - only the background fill does; drawn through a plain
 * RastPort on the bitmap it does.  A plain RastPort does not clip, so the icon is drawn into a work bitmap with
 * RENDER_SPARE pixels to spare on every side and the icon's own area is then copied out. */
#define RENDER_SPARE 16
struct BitMap *OwnRenderIcon(struct Screen *scr, LONG *w, LONG *h)
{
    struct DrawInfo *dri;
    struct BitMap *work;
    struct RastPort rp;
    ULONG depth;

    if (!ownIcon || !scr) return NULL;
    if (ownRender) { *w = ownIconW; *h = ownIconH; return ownRender; }
    if (!(dri = GetScreenDrawInfo(scr))) return NULL;
    depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    work = AllocBitMap(ownIconW + 2 * RENDER_SPARE, ownIconH + 2 * RENDER_SPARE, depth, BMF_MINPLANES, scr->RastPort.BitMap);
    if (work) {
        ownRender = AllocBitMap(ownIconW, ownIconH, depth, BMF_MINPLANES, scr->RastPort.BitMap);
        if (ownRender) {
            InitRastPort(&rp);
            rp.BitMap = work;
            SetAPen(&rp, dri->dri_Pens[BACKGROUNDPEN]);
            SetDrMd(&rp, JAM1);
            RectFill(&rp, 0, 0, ownIconW + 2 * RENDER_SPARE - 1, ownIconH + 2 * RENDER_SPARE - 1);
            icon_draw(&rp, ownIcon, dri, RENDER_SPARE, RENDER_SPARE);
            BltBitMap(work, RENDER_SPARE, RENDER_SPARE, ownRender, 0, 0, ownIconW, ownIconH, 0xC0, 0xFF, NULL);
        }
        WaitBlit();
        FreeBitMap(work);
    }
    FreeScreenDrawInfo(scr, dri);
    if (ownRender) { *w = ownIconW; *h = ownIconH; }
    return ownRender;
}

void OwnCleanup(void)
{
    if (ownRender) { WaitBlit(); FreeBitMap(ownRender); ownRender = NULL; }
    if (ownIcon) { FreeDiskObject(ownIcon); ownIcon = NULL; }
    if (GadToolsBase) { CloseLibrary(GadToolsBase); GadToolsBase = NULL; }
    if (IconBase) { CloseLibrary(IconBase); IconBase = NULL; }
}

/* ---- the requester ------------------------------------------------------------------------------------------- */

struct OwnLayout {
    struct RastPort *rp;
    struct DrawInfo *dri;
    APTR vi;
    CONST_STRPTR body;
    LONG picW, picH;
    LONG boxX, boxY, boxW, boxH;      /* the body box, window coordinates */
    LONG textW, fh, baseline;
    LONG wrap;                        /* pixel width the text is wrapped to, 0 = lines as they are */
    LONG rows;                        /* text rows after wrapping */
    LONG visible;                     /* rows shown at a time */
    LONG first;                       /* first row shown */
    BOOL scroll;                      /* more rows than fit: cursor keys move, the last row is a hint */
};

typedef void (*RowFn)(struct OwnLayout *, CONST_STRPTR, LONG, LONG);

/* Walks the rows of the body text: every line (LF) is a row; with L->wrap a line wider than that many pixels is
 * broken after the last blank that fits (or where it must).  Calls f(row text, length, row number); returns the
 * number of rows.  L->rp must carry the font. */
static LONG each_row(struct OwnLayout *L, RowFn f)
{
    LONG row = 0;
    CONST_STRPTR s = L->body;

    for (;;) {
        CONST_STRPTR e = s;
        LONG len;

        while (*e && *e != '\n') e++;
        len = e - s;
        if (!L->wrap || !len) {
            if (f) f(L, s, len, row);
            row++;
        } else {
            while (len > 0) {
                struct TextExtent te;
                LONG fit = TextFit(L->rp, (STRPTR)s, len, &te, NULL, 1, L->wrap, L->fh + 1);

                if (fit <= 0) fit = 1;                      /* one glyph wider than the box: move on all the same */
                if (fit < len) {
                    LONG k = fit;
                    while (k > 0 && s[k - 1] != ' ') k--;
                    if (k > 0) fit = k;                     /* break after the last blank that fits */
                }
                if (f) f(L, s, fit, row);
                row++;
                s += fit;
                len -= fit;
            }
        }
        if (!*e) break;
        s = e + 1;
    }
    return row;
}

static void measure_row(struct OwnLayout *L, CONST_STRPTR s, LONG len, LONG row)
{
    LONG w = len ? TextLength(L->rp, (STRPTR)s, len) : 0;
    (void)row;
    if (w > L->textW) L->textW = w;
}

static LONG text_left(struct OwnLayout *L) { return L->boxX + PAD + (L->picW ? L->picW + GAP : 0); }

static LONG text_top(struct OwnLayout *L)
{
    LONG h = (L->visible + (L->scroll ? 1 : 0)) * L->fh;
    return L->boxY + PAD + (L->picH > h ? (L->picH - h) / 2 : 0);
}

static void put_text(struct OwnLayout *L, CONST_STRPTR s, LONG len, LONG line)
{
    LONG x = text_left(L);
    LONG room = L->boxX + L->boxW - PAD - x;
    struct TextExtent te;

    if (len <= 0 || room <= 0) return;
    len = TextFit(L->rp, (STRPTR)s, len, &te, NULL, 1, room, L->fh + 1);          /* never beyond the box */
    if (len > 0) {
        Move(L->rp, x, text_top(L) + line * L->fh + L->baseline);
        Text(L->rp, (STRPTR)s, len);
    }
}

static void draw_row(struct OwnLayout *L, CONST_STRPTR s, LONG len, LONG row)
{
    if (row >= L->first && row < L->first + L->visible) put_text(L, s, len, row - L->first);
}

static void draw_all(struct OwnLayout *L, BOOL erase)
{
    struct TagItem bt[] = { { GT_VisualInfo, (ULONG)L->vi }, { GTBB_Recessed, TRUE }, { TAG_DONE, 0 } };
    LONG py = L->boxY + (L->boxH - L->picH) / 2;

    if (erase) {
        SetAPen(L->rp, L->dri->dri_Pens[BACKGROUNDPEN]);
        SetDrMd(L->rp, JAM1);
        RectFill(L->rp, L->boxX, L->boxY, L->boxX + L->boxW - 1, L->boxY + L->boxH - 1);
    }
    DrawBevelBoxA(L->rp, L->boxX, L->boxY, L->boxW, L->boxH, bt);
    if (L->picW) {
        if (ownIcon && ownRender && OwnTestMode() >= 2)
            BltBitMapRastPort(ownRender, 0, 0, L->rp, L->boxX + PAD, py, L->picW, L->picH, 0xC0);
        else if (ownIcon) icon_draw(L->rp, ownIcon, L->dri, L->boxX + PAD, py);
        else if (ownPicture) BltBitMapRastPort(ownPicture, 0, 0, L->rp, L->boxX + PAD, py, L->picW, L->picH, 0xC0);
    }
    SetAPen(L->rp, L->dri->dri_Pens[TEXTPEN]);
    SetDrMd(L->rp, JAM1);
    each_row(L, draw_row);
    if (L->scroll) {
        char hint[80];
        LONG last = L->first + L->visible;

        if (last > L->rows) last = L->rows;
        sprintf(hint, "[ rows %ld-%ld of %ld - cursor keys or space for more ]", (long)(L->first + 1), (long)last, (long)L->rows);
        SetAPen(L->rp, L->dri->dri_Pens[HIGHLIGHTTEXTPEN]);
        put_text(L, (CONST_STRPTR)hint, strlen(hint), L->visible);
    }
}

static BOOL scroll_to(struct OwnLayout *L, LONG first)
{
    if (first > L->rows - L->visible) first = L->rows - L->visible;
    if (first < 0) first = 0;
    if (first == L->first) return FALSE;
    L->first = first;
    return TRUE;
}

/* What the class's requester needs more than ours without a picture - CALIBRATED, not read from anywhere:
 * measured 13.11.2026 on WinUAE (OS 3.9, requester.class 42.10, 1366x768, the screen font), class window against
 * our window WITH a 48-pixel icon for the same three texts: 559x224 / 591x237, 398x146 / 430x159, 552x94 / 584x115.
 * Ours with the icon is 56 wider than ours without (48 + GAP), so the class is 24 wider than ours without and never
 * taller.  A class or font that lays out differently falsifies it; the cost of an error is small either way (our
 * requester comes although the class's would have fitted, or the other way round as before 1.5). */
#define CLASS_EXTRA_W 24

/* A requester of our own: title, body (lines separated by LF), buttons "a|b|c" with "_" in front of the hot key,
 * and the picture loaded before (icon or bitmap), if any.
 * It comes when there is a picture - the class below V47 cannot show one - or when the text is larger than the
 * screen: the class's requester is then cut off at the screen edge and its buttons cannot be reached with the mouse
 * (measured: The Butcher's 25-line ReadMe on a 640x256 screen, requester.class 42.10).  Ours never gets larger than
 * the screen: long lines are wrapped, and when there are more rows than fit, the cursor keys (Shift = a page), space
 * and backspace move through them; the buttons are always there.
 * Returns what requester.class returns for the same buttons - 0 for the rightmost, 1, 2 ... from the left for the
 * others - or -1 when it is not needed or could not be made (the caller then opens the class's requester as
 * before).  Ctrl-C to the program answers like the rightmost button.
 * debug: print the window and button boxes (screen coordinates). */
LONG OwnRequester(struct Screen *pubScreen, CONST_STRPTR title, CONST_STRPTR body, CONST_STRPTR gadgets, BOOL debug)
{
    struct Screen *scr = pubScreen, *locked = NULL;
    struct OwnLayout L;
    struct RastPort trp;
    struct TextFont *font;
    struct Window *win = NULL;
    struct Gadget *glist = NULL, *gad, *btn[MAXBUTTONS];
    struct NewGadget ng;
    char *labels = NULL, *lab[MAXBUTTONS];
    char hot[MAXBUTTONS];
    LONG labw[MAXBUTTONS];
    LONG n = 0, i, result = -1, btnW, btnH, innerW, innerH, minBtn = 0, top, left, maxW, maxH, titleW, picCol;
    BOOL done = FALSE, toobig = FALSE;

    if (!title || !body || !gadgets || !gadgets[0]) return -1;
    if (!GadToolsBase && !(GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 37))) return -1;
    if (!scr && !(scr = locked = LockPubScreen(NULL))) return -1;
    memset(&L, 0, sizeof(L));
    if (!(L.dri = GetScreenDrawInfo(scr))) goto out;
    if (!(L.vi = GetVisualInfoA(scr, NULL))) goto out;
    font = L.dri->dri_Font;
    if (!font) goto out;
    if (!(labels = AllocVec(strlen((const char *)gadgets) + 1, MEMF_ANY))) goto out;
    strcpy(labels, (const char *)gadgets);

    /* the buttons: text, hot key, text width without the hot-key underscore */
    InitRastPort(&trp);
    SetFont(&trp, font);
    {
        char *s = labels;
        while (n < MAXBUTTONS) {
            char *e = s, *u;
            while (*e && *e != '|') e++;
            lab[n] = s;
            hot[n] = 0;
            labw[n] = 0;
            if (*e) { *e = 0; e++; } else e = NULL;
            /* The FIRST underscore marks the hot key, any further one is text - that is how requester.class
               shows "_Reboot_NOW" (measured: "Reboot_NOW", R underlined).  GadTools given '_' strips them all
               (measured: "RebootNOW", N underlined), so the first one becomes HOTMARK and GadTools gets that. */
            u = strchr(s, '_');
            if (u && u[1]) {
                hot[n] = (char)lower((unsigned char)u[1]);
                *u = HOTMARK;
                labw[n] = TextLength(&trp, (STRPTR)s, u - s) + TextLength(&trp, (STRPTR)(u + 1), strlen(u + 1));
            } else
                labw[n] = TextLength(&trp, (STRPTR)s, strlen(s));
            if (labw[n] > minBtn) minBtn = labw[n];
            n++;
            if (!e) break;
            s = e;
        }
    }
    if (n < 1) goto out;

    /* sizes: first the text as it is, then wrapped and cut down to what the screen can show */
    L.rp = &trp;
    L.body = body;
    L.fh = font->tf_YSize;
    L.baseline = font->tf_Baseline;
    L.picW = ownIcon ? ownIconW : (ownPicture ? ownPicW : 0);
    L.picH = ownIcon ? ownIconH : (ownPicture ? ownPicH : 0);
    btnH = L.fh + 6;
    minBtn += 20;
    maxW = scr->Width - scr->WBorLeft - scr->WBorRight - 2 * OUTER - 16;
    maxH = scr->Height - (scr->WBorTop + scr->Font->ta_YSize + 1) - scr->WBorBottom - 2 * OUTER - 16 - BTNGAP - btnH;
    if (maxH < 2 * PAD + 2 * L.fh || maxW < n * 24) goto out;          /* a screen too small for any of this */
    if (L.picH > maxH - 2 * PAD || L.picW > maxW / 2) L.picW = L.picH = 0;     /* no room for the picture */
    picCol = L.picW ? L.picW + GAP : 0;
    L.rows = each_row(&L, measure_row);
    if (2 * PAD + L.textW + CLASS_EXTRA_W > maxW || 2 * PAD + L.rows * L.fh > maxH) toobig = TRUE;   /* for the class */
    if (!L.picW && !toobig) goto out;                                  /* nothing the class cannot do itself */
    if (2 * PAD + picCol + L.textW > maxW) {
        L.wrap = maxW - 2 * PAD - picCol;
        L.textW = 0;
        L.rows = each_row(&L, measure_row);
        if (L.textW > L.wrap) L.textW = L.wrap;
    }
    L.visible = L.rows;
    if (2 * PAD + L.rows * L.fh > maxH) {
        L.scroll = TRUE;
        L.visible = (maxH - 2 * PAD) / L.fh - 1;                       /* one row is the hint */
        if (L.visible < 1) goto out;
        {
            static const char widest[] = "[ rows 000-000 of 000 - cursor keys or space for more ]";
            i = TextLength(&trp, (STRPTR)widest, sizeof(widest) - 1);
        }
        if (i > L.textW) L.textW = (2 * PAD + picCol + i > maxW) ? maxW - 2 * PAD - picCol : i;
    }
    titleW = TextLength(&scr->RastPort, (STRPTR)title, strlen((const char *)title)) + 60;    /* room for the depth gadget */
    L.boxW = 2 * PAD + picCol + L.textW;
    L.boxH = 2 * PAD + (L.visible + (L.scroll ? 1 : 0)) * L.fh;
    if (L.boxH < 2 * PAD + L.picH) L.boxH = 2 * PAD + L.picH;
    innerW = L.boxW;
    if (innerW < n * minBtn + (n - 1) * BTNGAP) innerW = n * minBtn + (n - 1) * BTNGAP;
    if (innerW < titleW) innerW = titleW;
    if (innerW > maxW) innerW = maxW;
    L.boxW = innerW;
    innerH = L.boxH + BTNGAP + btnH;
    btnW = (innerW - (n - 1) * BTNGAP) / n;
    left = scr->WBorLeft + OUTER;
    top = scr->WBorTop + scr->Font->ta_YSize + 1 + OUTER;
    L.boxX = left;
    L.boxY = top;

    /* the gadgets */
    if (!(gad = CreateContext(&glist))) goto out;
    for (i = 0; i < n; i++) {
        struct TagItem gt[] = { { GT_Underscore, HOTMARK }, { TAG_DONE, 0 } };
        memset(&ng, 0, sizeof(ng));
        ng.ng_LeftEdge = left + i * (btnW + BTNGAP);
        ng.ng_TopEdge = top + L.boxH + BTNGAP;
        ng.ng_Width = (i == n - 1) ? left + innerW - ng.ng_LeftEdge : btnW;       /* the last one takes the rest */
        ng.ng_Height = btnH;
        ng.ng_GadgetText = (UBYTE *)lab[i];
        ng.ng_TextAttr = scr->Font;
        ng.ng_GadgetID = i;
        ng.ng_Flags = PLACETEXT_IN;
        ng.ng_VisualInfo = L.vi;
        gad = btn[i] = CreateGadgetA(BUTTON_KIND, gad, &ng, gt);
    }
    if (!gad) goto out;

    {
        LONG ww = innerW + 2 * OUTER + scr->WBorLeft + scr->WBorRight;
        LONG wh = innerH + 2 * OUTER + scr->WBorTop + scr->Font->ta_YSize + 1 + scr->WBorBottom;
        struct TagItem wt[] = {
            { WA_Left, (scr->Width - ww) / 2 }, { WA_Top, (scr->Height - wh) / 2 },
            { WA_Width, ww }, { WA_Height, wh },
            { WA_Title, (ULONG)title }, { WA_PubScreen, (ULONG)scr }, { WA_Gadgets, (ULONG)glist },
            { WA_IDCMP, IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_RAWKEY },
            { WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_ACTIVATE | WFLG_SMART_REFRESH | WFLG_RMBTRAP },
            { WA_AutoAdjust, TRUE }, { TAG_DONE, 0 } };
        win = OpenWindowTagList(NULL, wt);
    }
    if (!win) goto out;
    GT_RefreshWindow(win, NULL);
    SetFont(win->RPort, font);
    L.rp = win->RPort;
    draw_all(&L, FALSE);
    if (debug) {
        printf("ownreq: window %d %d %d %d, text rows %ld, shown %ld%s%s\n", win->LeftEdge, win->TopEdge, win->Width,
               win->Height, (long)L.rows, (long)L.visible, L.wrap ? ", wrapped" : "", toobig ? ", too big for the class" : "");
        for (i = 0; i < n; i++) {
            char show[40];
            LONG k;
            for (k = 0; k < (LONG)sizeof(show) - 1 && lab[i][k]; k++) show[k] = (lab[i][k] == HOTMARK) ? '_' : lab[i][k];
            show[k] = 0;
            printf("ownreq: button %ld at %d %d %d %d \"%s\"\n", (long)i, win->LeftEdge + btn[i]->LeftEdge,
                   win->TopEdge + btn[i]->TopEdge, btn[i]->Width, btn[i]->Height, show);
        }
        fflush(stdout);
    }

    while (!done) {
        ULONG sig = Wait((1UL << win->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        struct IntuiMessage *m;

        if (sig & SIGBREAKF_CTRL_C) { result = 0; done = TRUE; }
        while ((m = GT_GetIMsg(win->UserPort))) {
            ULONG cl = m->Class;
            UWORD code = m->Code, qual = m->Qualifier;
            struct Gadget *g = (struct Gadget *)m->IAddress;
            LONG move = 0;

            GT_ReplyIMsg(m);
            if (cl == IDCMP_GADGETUP && g && g->GadgetID < n) {
                result = (g->GadgetID == n - 1) ? 0 : g->GadgetID + 1;
                done = TRUE;
            } else if (cl == IDCMP_VANILLAKEY) {
                for (i = 0; i < n; i++)
                    if (hot[i] && hot[i] == (char)lower(code & 0xFF)) {
                        result = (i == n - 1) ? 0 : i + 1;
                        done = TRUE;
                        break;
                    }
                if (!done && code == ' ') move = L.visible;
                if (!done && code == 8) move = -L.visible;
            } else if (cl == IDCMP_RAWKEY) {
                LONG step = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? L.visible : 1;
                if (code == 0x4D) move = step;             /* cursor down */
                if (code == 0x4C) move = -step;            /* cursor up */
            } else if (cl == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(win);
                draw_all(&L, FALSE);
                GT_EndRefresh(win, TRUE);
            }
            if (move && L.scroll && !done && scroll_to(&L, L.first + move)) draw_all(&L, TRUE);
        }
    }

out:
    if (win) CloseWindow(win);
    if (glist) FreeGadgets(glist);
    if (labels) FreeVec(labels);
    if (L.vi) FreeVisualInfo(L.vi);
    if (L.dri) FreeScreenDrawInfo(scr, L.dri);
    if (locked) UnlockPubScreen(NULL, locked);
    return result;
}

/* ---- test builds only: how deep does the stack go? ----------------------------------------------------------- */
#ifdef OWNREQ_TEST
#include <dos/dosextens.h>

static UBYTE *stkLower = NULL, *stkUpper = NULL;

/* Bounds of the stack this program runs on: for a command started by a Shell they are behind pr_ReturnAddr (Guru
 * Book p.42: upper = pr_ReturnAddr + 4, size = *(ULONG *)pr_ReturnAddr), otherwise the task's own.  Everything from
 * the lower bound up to 256 bytes below the present stack pointer is filled with a pattern. */
void OwnStackMark(void)
{
    struct Process *pr = (struct Process *)FindTask(NULL);
    UBYTE *here = (UBYTE *)&pr, *p;

    if (pr->pr_Task.tc_Node.ln_Type == NT_PROCESS && pr->pr_CLI && pr->pr_ReturnAddr) {
        stkUpper = (UBYTE *)pr->pr_ReturnAddr + sizeof(ULONG);
        stkLower = stkUpper - *(ULONG *)pr->pr_ReturnAddr;
    } else {
        stkUpper = (UBYTE *)pr->pr_Task.tc_SPUpper;
        stkLower = (UBYTE *)pr->pr_Task.tc_SPLower;
    }
    if (!(stkLower < here && here < stkUpper) || here - stkLower < 512) { stkLower = stkUpper = NULL; return; }
    for (p = stkLower; p < here - 256; p++) *p = 0xA5;
}

void OwnStackReport(void)
{
    UBYTE *p;
    ULONG first = 0, skipped = 0;

    if (!stkLower) { printf("stack: bounds not found, nothing measured\n"); return; }
    for (p = stkLower; p < stkUpper && *p == 0xA5; p++) ;
    if (p + 4 <= stkUpper) first = ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
    /* exec 45.20's StackSwap() writes $BAD1BAD3 near the bottom of the stack it leaves: that is not stack use */
    if (first == 0xBAD1BAD3UL) {
        skipped = 1;
        for (p += 4; p < stkUpper && *p == 0xA5; p++) ;
    }
    printf("stack: size %lu, deepest use %lu bytes, first word found $%08lx%s\n", (unsigned long)(stkUpper - stkLower),
           (unsigned long)(stkUpper - p), (unsigned long)first, skipped ? " (StackSwap mark, skipped)" : "");
}
#endif
