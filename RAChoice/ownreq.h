/* ownreq.h - RAChoice 1.5: icon loader and own requester window (see ownreq.c). */
#ifndef OWNREQ_H
#define OWNREQ_H

#include <exec/types.h>

struct Screen;
struct BitMap;

BOOL OwnHasPicture(void);
void OwnSetPicture(struct BitMap *bm, LONG w, LONG h);
BOOL OwnLoadIcon(CONST_STRPTR name, struct Screen *scr);
struct BitMap *OwnRenderIcon(struct Screen *scr, LONG *w, LONG *h);
LONG OwnRequester(struct Screen *pubScreen, CONST_STRPTR title, CONST_STRPTR body, CONST_STRPTR gadgets, BOOL debug);
void OwnCleanup(void);
int  OwnTestMode(void);    /* 0 in a release build */
#ifdef OWNREQ_TEST
void OwnStackMark(void);
void OwnStackReport(void);
#endif

#endif
