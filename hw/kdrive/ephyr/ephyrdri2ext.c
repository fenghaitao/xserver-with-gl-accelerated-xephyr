/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 * 
 * Copyright © 2007 OpenedHand Ltd 
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of OpenedHand Ltd not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. OpenedHand Ltd makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * OpenedHand Ltd DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OpenedHand Ltd BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * This file is heavily copied from hw/xfree86/dri/xf86dri.c
 *
 * Authors:
 *    Haitao Feng <haitao.feng@intel.com>
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <string.h>

#define NEED_REPLIES
#define NEED_EVENTS
#include <X11/X.h>
#include <X11/Xproto.h>
#include "misc.h"
#include "privates.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "colormapst.h"
#include "cursorstr.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "servermd.h"
#include "swaprep.h"
#include "ephyrdri.h"
#include "ephyrdriext.h"
#include "hostx.h"
#define _HAVE_XALLOC_DECLS
#include "ephyrlog.h"

#include "ephyrdri2ext.h"

#include <X11/extensions/dri2proto.h>
#include "ephyrdri2.h"
#include <X11/extensions/xfixeswire.h>
#include <X11/Xlib.h>

extern RESTYPE	RegionResType;
extern int	XFixesErrorBase;
#define VERIFY_REGION(pRegion, rid, client, mode) { \
    pRegion = SecurityLookupIDByType (client, rid, RegionResType, mode); \
    if (!pRegion) { \
	client->errorValue = rid; \
	return XFixesErrorBase + BadRegion; \
    } \
}
typedef struct {
    int foo;
} EphyrDRI2WindowPrivRec;
typedef EphyrDRI2WindowPrivRec* EphyrDRI2WindowPrivPtr;

typedef struct {
    CreateWindowProcPtr CreateWindow ;
    DestroyWindowProcPtr DestroyWindow ;
    MoveWindowProcPtr MoveWindow ;
    PositionWindowProcPtr PositionWindow ;
    ClipNotifyProcPtr ClipNotify ;
} EphyrDRI2ScreenPrivRec;
typedef EphyrDRI2ScreenPrivRec* EphyrDRI2ScreenPrivPtr;

static DISPATCH_PROC(ProcDRI2QueryVersion);
static DISPATCH_PROC(ProcDRI2Connect);
static DISPATCH_PROC(ProcDRI2Authenticate);
static DISPATCH_PROC(ProcDRI2CreateDrawable);
static DISPATCH_PROC(ProcDRI2DestroyDrawable);
static DISPATCH_PROC(ProcDRI2GetBuffers);
static DISPATCH_PROC(ProcDRI2GetBuffersWithFormat);
static DISPATCH_PROC(ProcDRI2CopyRegion);
static DISPATCH_PROC(ProcDRI2Dispatch);
static DISPATCH_PROC(SProcDRI2Connect);
static DISPATCH_PROC(SProcDRI2Dispatch);

static Bool ephyrDRI2ScreenInit (ScreenPtr a_screen) ;
static Bool ephyrDRI2CreateWindow (WindowPtr a_win) ;
static Bool ephyrDRI2DestroyWindow (WindowPtr a_win) ;
static void ephyrDRI2MoveWindow (WindowPtr a_win,
                                int a_x, int a_y,
                                WindowPtr a_siblings,
                                VTKind a_kind);
static Bool ephyrDRI2PositionWindow (WindowPtr a_win,
                                    int x, int y) ;
static void ephyrDRI2ClipNotify (WindowPtr a_win,
                                int a_x, int a_y) ;

static Bool EphyrMirrorHostVisuals (ScreenPtr a_screen) ;
static Bool destroyHostPeerWindow (const WindowPtr a_win) ;
static Bool findWindowPairFromLocal (WindowPtr a_local,
                                     EphyrDRI2WindowPair **a_pair);

static int ephyrDRI2WindowKeyIndex;
static DevPrivateKey ephyrDRI2WindowKey = &ephyrDRI2WindowKeyIndex;
static int ephyrDRI2ScreenKeyIndex;
static DevPrivateKey ephyrDRI2ScreenKey = &ephyrDRI2ScreenKeyIndex;

#define GET_EPHYR_DRI2_WINDOW_PRIV(win) ((EphyrDRI2WindowPrivPtr) \
    dixLookupPrivate(&(win)->devPrivates, ephyrDRI2WindowKey))
#define GET_EPHYR_DRI2_SCREEN_PRIV(screen) ((EphyrDRI2ScreenPrivPtr) \
    dixLookupPrivate(&(screen)->devPrivates, ephyrDRI2ScreenKey))

static ExtensionEntry	*dri2Extension;
static RESTYPE		 dri2DrawableRes;

static int DRI2DrawableGone(pointer p, XID id)
{
    DrawablePtr pDrawable = p;
    WindowPtr window 	  = (WindowPtr)pDrawable;
    EphyrDRI2WindowPair *pair = NULL;

    memset (&pair, 0, sizeof (pair)) ;
    if (!findWindowPairFromLocal (window, &pair) || !pair) {
        EPHYR_LOG_ERROR ("failed to find remote peer drawable\n") ;
        return BadMatch ;
    }
    ephyrDRI2DestroyDrawable(pair->remote);

    return Success;
}

Bool
ephyrDRI2ExtensionInit (ScreenPtr a_screen)
{
    Bool is_ok=FALSE ;
    EphyrDRI2ScreenPrivPtr screen_priv=NULL ;

    EPHYR_LOG ("enter\n") ;
    if (!hostx_has_dri2 ()) {
        EPHYR_LOG ("host does not have DRI2 extension\n") ;
        goto out ;
    }
    EPHYR_LOG ("host X does have DRI2 extension\n") ;

    if (!hostx_has_xshape ()) {
        EPHYR_LOG ("host does not have XShape extension\n") ;
        goto out ;
    }
    EPHYR_LOG ("host X does have XShape extension\n") ;

    dri2Extension = AddExtension(DRI2_NAME,
				 DRI2NumberEvents,
				 DRI2NumberErrors,
				 ProcDRI2Dispatch,
				 SProcDRI2Dispatch,
				 NULL,
				 StandardMinorOpcode);

    if (!dri2Extension){
        EPHYR_LOG_ERROR ("failed to register DRI extension\n") ;
        goto out;
    }

    dri2DrawableRes = CreateNewResourceType(DRI2DrawableGone, "DRI2Drawable");

    screen_priv = xcalloc (1, sizeof (EphyrDRI2ScreenPrivRec)) ;
    if (!screen_priv) {
        EPHYR_LOG_ERROR ("failed to allocate screen_priv\n") ;
        goto out ;
    }
    dixSetPrivate(&a_screen->devPrivates, ephyrDRI2ScreenKey, screen_priv);

    if (!ephyrDRI2ScreenInit (a_screen)) {
        EPHYR_LOG_ERROR ("ephyrDRI2ScreenInit() failed\n") ;
        goto out ;
    }
    EphyrMirrorHostVisuals (a_screen) ;
    is_ok=TRUE ;
out:
    EPHYR_LOG ("leave\n") ;
    return is_ok ;
}

static Bool
ephyrDRI2ScreenInit (ScreenPtr a_screen)
{
    Bool is_ok=FALSE ;
    EphyrDRI2ScreenPrivPtr screen_priv=NULL ;

    EPHYR_RETURN_VAL_IF_FAIL (a_screen, FALSE) ;

    screen_priv=GET_EPHYR_DRI2_SCREEN_PRIV (a_screen) ;
    EPHYR_RETURN_VAL_IF_FAIL (screen_priv, FALSE) ;

    screen_priv->CreateWindow = a_screen->CreateWindow ;
    screen_priv->DestroyWindow = a_screen->DestroyWindow ;
    screen_priv->MoveWindow = a_screen->MoveWindow ;
    screen_priv->PositionWindow = a_screen->PositionWindow ;
    screen_priv->ClipNotify = a_screen->ClipNotify ;

    a_screen->CreateWindow = ephyrDRI2CreateWindow ;
    a_screen->DestroyWindow = ephyrDRI2DestroyWindow ;
    a_screen->MoveWindow = ephyrDRI2MoveWindow ;
    a_screen->PositionWindow = ephyrDRI2PositionWindow ;
    a_screen->ClipNotify = ephyrDRI2ClipNotify ;

    is_ok = TRUE ;

    return is_ok ;
}

static Bool
ephyrDRI2CreateWindow (WindowPtr a_win)
{
    Bool is_ok=FALSE ;
    ScreenPtr screen=NULL ;
    EphyrDRI2ScreenPrivPtr screen_priv =NULL;

    EPHYR_RETURN_VAL_IF_FAIL (a_win, FALSE) ;
    screen = a_win->drawable.pScreen ;
    EPHYR_RETURN_VAL_IF_FAIL (screen, FALSE) ;
    screen_priv = GET_EPHYR_DRI2_SCREEN_PRIV (screen) ;
    EPHYR_RETURN_VAL_IF_FAIL (screen_priv
                              && screen_priv->CreateWindow,
                              FALSE) ;

    EPHYR_LOG ("enter. win:%p\n", a_win) ;

    screen->CreateWindow = screen_priv->CreateWindow ;
    is_ok = (*screen->CreateWindow) (a_win) ;
    screen->CreateWindow = ephyrDRI2CreateWindow ;

    if (is_ok) {
	dixSetPrivate(&a_win->devPrivates, ephyrDRI2WindowKey, NULL);
    }
    return is_ok ;
}

static Bool
ephyrDRI2DestroyWindow (WindowPtr a_win)
{
    Bool is_ok=FALSE ;
    ScreenPtr screen=NULL ;
    EphyrDRI2ScreenPrivPtr screen_priv =NULL;

    EPHYR_RETURN_VAL_IF_FAIL (a_win, FALSE) ;
    screen = a_win->drawable.pScreen ;
    EPHYR_RETURN_VAL_IF_FAIL (screen, FALSE) ;
    screen_priv = GET_EPHYR_DRI2_SCREEN_PRIV (screen) ;
    EPHYR_RETURN_VAL_IF_FAIL (screen_priv
                              && screen_priv->DestroyWindow,
                              FALSE) ;

    screen->DestroyWindow = screen_priv->DestroyWindow ;
    if (screen->DestroyWindow) {
        is_ok = (*screen->DestroyWindow) (a_win) ;
    }
    screen->DestroyWindow = ephyrDRI2DestroyWindow ;

    if (is_ok) {
        EphyrDRI2WindowPrivPtr win_priv=GET_EPHYR_DRI2_WINDOW_PRIV (a_win) ;
        if (win_priv) {
            destroyHostPeerWindow (a_win) ;
            xfree (win_priv) ;
            dixSetPrivate(&a_win->devPrivates, ephyrDRI2WindowKey, NULL);
            EPHYR_LOG ("destroyed the remote peer window\n") ;
        }
    }
    return is_ok ;
}

static void
ephyrDRI2MoveWindow (WindowPtr a_win,
                    int a_x, int a_y,
                    WindowPtr a_siblings,
                    VTKind a_kind)
{
    Bool is_ok=FALSE ;
    ScreenPtr screen=NULL ;
    EphyrDRI2ScreenPrivPtr screen_priv =NULL;
    EphyrDRI2WindowPrivPtr win_priv=NULL ;
    EphyrDRI2WindowPair *pair=NULL ;
    EphyrBox geo;
    int x=0,y=0;/*coords relative to parent window*/

    EPHYR_RETURN_IF_FAIL (a_win) ;

    EPHYR_LOG ("enter\n") ;
    screen = a_win->drawable.pScreen ;
    EPHYR_RETURN_IF_FAIL (screen) ;
    screen_priv = GET_EPHYR_DRI2_SCREEN_PRIV (screen) ;
    EPHYR_RETURN_IF_FAIL (screen_priv
                          && screen_priv->MoveWindow) ;

    screen->MoveWindow = screen_priv->MoveWindow ;
    if (screen->MoveWindow) {
        (*screen->MoveWindow) (a_win, a_x, a_y, a_siblings, a_kind) ;
    }
    screen->MoveWindow = ephyrDRI2MoveWindow ;

    EPHYR_LOG ("window: %p\n", a_win) ;
    if (!a_win->parent) {
        EPHYR_LOG ("cannot move root window\n") ;
        is_ok = TRUE ;
        goto out ;
    }
    win_priv = GET_EPHYR_DRI2_WINDOW_PRIV (a_win) ;
    if (!win_priv) {
        EPHYR_LOG ("not a DRI peered window\n") ;
        is_ok = TRUE ;
        goto out ;
    }
    if (!findWindowPairFromLocal (a_win, &pair) || !pair) {
        EPHYR_LOG_ERROR ("failed to get window pair\n") ;
        goto out ;
    }
    /*compute position relative to parent window*/
    x = a_win->drawable.x - a_win->parent->drawable.x ;
    y = a_win->drawable.y - a_win->parent->drawable.y ;
    /*set the geometry to pass to hostx_set_window_geometry*/
    memset (&geo, 0, sizeof (geo)) ;
    geo.x = x ;
    geo.y = y ;
    geo.width = a_win->drawable.width ;
    geo.height = a_win->drawable.height ;
    hostx_set_window_geometry (pair->remote, &geo) ;
    is_ok = TRUE ;

out:
    EPHYR_LOG ("leave. is_ok:%d\n", is_ok) ;
    /*do cleanup here*/
}

static Bool
ephyrDRI2PositionWindow (WindowPtr a_win,
                        int a_x, int a_y)
{
    Bool is_ok=FALSE ;
    ScreenPtr screen=NULL ;
    EphyrDRI2ScreenPrivPtr screen_priv =NULL;
    EphyrDRI2WindowPrivPtr win_priv=NULL ;
    EphyrDRI2WindowPair *pair=NULL ;
    EphyrBox geo;

    EPHYR_RETURN_VAL_IF_FAIL (a_win, FALSE) ;

    EPHYR_LOG ("enter\n") ;
    screen = a_win->drawable.pScreen ;
    EPHYR_RETURN_VAL_IF_FAIL (screen, FALSE) ;
    screen_priv = GET_EPHYR_DRI2_SCREEN_PRIV (screen) ;
    EPHYR_RETURN_VAL_IF_FAIL (screen_priv
                              && screen_priv->PositionWindow,
                              FALSE) ;

    screen->PositionWindow = screen_priv->PositionWindow ;
    if (screen->PositionWindow) {
        (*screen->PositionWindow) (a_win, a_x, a_y) ;
    }
    screen->PositionWindow = ephyrDRI2PositionWindow ;

    EPHYR_LOG ("window: %p\n", a_win) ;
    win_priv = GET_EPHYR_DRI2_WINDOW_PRIV (a_win) ;
    if (!win_priv) {
        EPHYR_LOG ("not a DRI peered window\n") ;
        is_ok = TRUE ;
        goto out ;
    }
    if (!findWindowPairFromLocal (a_win, &pair) || !pair) {
        EPHYR_LOG_ERROR ("failed to get window pair\n") ;
        goto out ;
    }
    /*set the geometry to pass to hostx_set_window_geometry*/
    memset (&geo, 0, sizeof (geo)) ;
    geo.x = a_x ;
    geo.y = a_y ;
    geo.width = a_win->drawable.width ;
    geo.height = a_win->drawable.height ;
    hostx_set_window_geometry (pair->remote, &geo) ;
    is_ok = TRUE ;

out:
    EPHYR_LOG ("leave. is_ok:%d\n", is_ok) ;
    /*do cleanup here*/
    return is_ok ;
}

static void
ephyrDRI2ClipNotify (WindowPtr a_win,
                    int a_x, int a_y)
{
    Bool is_ok=FALSE ;
    ScreenPtr screen=NULL ;
    EphyrDRI2ScreenPrivPtr screen_priv =NULL;
    EphyrDRI2WindowPrivPtr win_priv=NULL ;
    EphyrDRI2WindowPair *pair=NULL ;
    EphyrRect *rects=NULL;
    int i=0 ;

    EPHYR_RETURN_IF_FAIL (a_win) ;

    EPHYR_LOG ("enter\n") ;
    screen = a_win->drawable.pScreen ;
    EPHYR_RETURN_IF_FAIL (screen) ;
    screen_priv = GET_EPHYR_DRI2_SCREEN_PRIV (screen) ;
    EPHYR_RETURN_IF_FAIL (screen_priv && screen_priv->ClipNotify) ;

    screen->ClipNotify = screen_priv->ClipNotify ;
    if (screen->ClipNotify) {
        (*screen->ClipNotify) (a_win, a_x, a_y) ;
    }
    screen->ClipNotify = ephyrDRI2ClipNotify ;

    EPHYR_LOG ("window: %p\n", a_win) ;
    win_priv = GET_EPHYR_DRI2_WINDOW_PRIV (a_win) ;
    if (!win_priv) {
        EPHYR_LOG ("not a DRI peered window\n") ;
        is_ok = TRUE ;
        goto out ;
    }
    if (!findWindowPairFromLocal (a_win, &pair) || !pair) {
        EPHYR_LOG_ERROR ("failed to get window pair\n") ;
        goto out ;
    }
    rects = xcalloc (REGION_NUM_RECTS (&a_win->clipList),
                     sizeof (EphyrRect)) ;
    for (i=0; i < REGION_NUM_RECTS (&a_win->clipList); i++) {
        memmove (&rects[i],
                 &REGION_RECTS (&a_win->clipList)[i],
                 sizeof (EphyrRect)) ;
        rects[i].x1 -= a_win->drawable.x;
        rects[i].x2 -= a_win->drawable.x;
        rects[i].y1 -= a_win->drawable.y;
        rects[i].y2 -= a_win->drawable.y;
    }
    /*
     * push the clipping region of this window
     * to the peer window in the host
     */
    is_ok = hostx_set_window_bounding_rectangles
                                (pair->remote,
                                 rects,
                                 REGION_NUM_RECTS (&a_win->clipList)) ;
    is_ok = TRUE ;

out:
    if (rects) {
        xfree (rects) ;
        rects = NULL ;
    }
    EPHYR_LOG ("leave. is_ok:%d\n", is_ok) ;
    /*do cleanup here*/
}

/**
 * Duplicates a visual of a_screen
 * In screen a_screen, for depth a_depth, find a visual which
 * bitsPerRGBValue and colormap size equal
 * a_bits_per_rgb_values and a_colormap_entries.
 * The ID of that duplicated visual is set to a_new_id.
 * That duplicated visual is then added to the list of visuals
 * of the screen.
 */
static Bool
EphyrDuplicateVisual (unsigned int a_screen,
                      short a_depth,
                      short a_class,
                      short a_bits_per_rgb_values,
                      short a_colormap_entries,
                      unsigned int a_red_mask,
                      unsigned int a_green_mask,
                      unsigned int a_blue_mask,
                      unsigned int a_new_id)
{
    Bool is_ok = FALSE, found_visual=FALSE, found_depth=FALSE ;
    ScreenPtr screen=NULL ;
    VisualRec new_visual, *new_visuals=NULL ;
    int i=0 ;

    EPHYR_LOG ("enter\n") ; 
    if (a_screen > screenInfo.numScreens) {
        EPHYR_LOG_ERROR ("bad screen number\n") ;
        goto out;
    }
    memset (&new_visual, 0, sizeof (VisualRec)) ;

    /*get the screen pointed to by a_screen*/
    screen = screenInfo.screens[a_screen] ;
    EPHYR_RETURN_VAL_IF_FAIL (screen, FALSE) ;

    /*
     * In that screen, first look for an existing visual that has the
     * same characteristics as those passed in parameter
     * to this function and copy it.
     */
    for (i=0; i < screen->numVisuals; i++) {
        if (screen->visuals[i].bitsPerRGBValue == a_bits_per_rgb_values &&
            screen->visuals[i].ColormapEntries == a_colormap_entries ) {
            /*copy the visual found*/
            memcpy (&new_visual, &screen->visuals[i], sizeof (new_visual)) ;
            new_visual.vid = a_new_id ;
            new_visual.class = a_class ;
            new_visual.redMask = a_red_mask ;
            new_visual.greenMask = a_green_mask ;
            new_visual.blueMask = a_blue_mask ;
            found_visual = TRUE ;
            EPHYR_LOG ("found a visual that matches visual id: %d\n",
                       a_new_id) ;
            break;
        }
    }
    if (!found_visual) {
        EPHYR_LOG ("did not find any visual matching %d\n", a_new_id) ;
        goto out ;
    }
    /*
     * be prepare to extend screen->visuals to add new_visual to it
     */
    new_visuals = xcalloc (screen->numVisuals+1, sizeof (VisualRec)) ;
    memmove (new_visuals,
             screen->visuals,
             screen->numVisuals*sizeof (VisualRec)) ;
    memmove (&new_visuals[screen->numVisuals],
             &new_visual,
             sizeof (VisualRec)) ;
    /*
     * Now, in that same screen, update the screen->allowedDepths member.
     * In that array, each element represents the visuals applicable to
     * a given depth. So we need to add an entry matching the new visual
     * that we are going to add to screen->visuals
     */
    for (i=0; i<screen->numDepths; i++) {
        VisualID *vids=NULL;
        DepthPtr cur_depth=NULL ;
        /*find the entry matching a_depth*/
        if (screen->allowedDepths[i].depth != a_depth)
            continue ;
        cur_depth = &screen->allowedDepths[i];
        /*
         * extend the list of visual IDs in that entry,
         * so to add a_new_id in there.
         */
        vids = xrealloc (cur_depth->vids,
                         (cur_depth->numVids+1)*sizeof (VisualID));
        if (!vids) {
            EPHYR_LOG_ERROR ("failed to realloc numids\n") ;
            goto out ;
        }
        vids[cur_depth->numVids] = a_new_id ;
        /*
         * Okay now commit our change.
         * Do really update screen->allowedDepths[i]
         */
        cur_depth->numVids++ ;
        cur_depth->vids = vids ;
        found_depth=TRUE;
    }
    if (!found_depth) {
        EPHYR_LOG_ERROR ("failed to update screen[%d]->allowedDepth\n",
                         a_screen) ;
        goto out ;
    }
    /*
     * Commit our change to screen->visuals
     */
    xfree (screen->visuals) ;
    screen->visuals = new_visuals ;
    screen->numVisuals++ ;
    new_visuals = NULL ;

    is_ok = TRUE ;
out:
    if (new_visuals) {
        xfree (new_visuals) ;
        new_visuals = NULL ;
    }
    EPHYR_LOG ("leave\n") ; 
    return is_ok ;
}

/**
 * Duplicates the visuals of the host X server.
 * This is necessary to have visuals that have the same
 * ID as those of the host X. It is important to have that for
 * GLX.
 */
static Bool
EphyrMirrorHostVisuals (ScreenPtr a_screen)
{
    Bool is_ok=FALSE;
    EphyrHostVisualInfo  *visuals=NULL;
    int nb_visuals=0, i=0;

    EPHYR_LOG ("enter\n") ;
    if (!hostx_get_visuals_info (&visuals, &nb_visuals)) {
        EPHYR_LOG_ERROR ("failed to get host visuals\n") ;
        goto out ;
    }
    for (i=0; i<nb_visuals; i++) {
        if (!EphyrDuplicateVisual (a_screen->myNum,
                                   visuals[i].depth,
                                   visuals[i].class,
                                   visuals[i].bits_per_rgb,
                                   visuals[i].colormap_size,
                                   visuals[i].red_mask,
                                   visuals[i].green_mask,
                                   visuals[i].blue_mask,
                                   visuals[i].visualid)) {
            EPHYR_LOG_ERROR ("failed to duplicate host visual %d\n",
                             (int)visuals[i].visualid) ;
        }
    }

    is_ok = TRUE ;
out:
    EPHYR_LOG ("leave\n") ;
    return is_ok;
}

static Bool
getWindowVisual (const WindowPtr a_win,
                 VisualPtr *a_visual)
{
    int i=0, visual_id=0 ;
    EPHYR_RETURN_VAL_IF_FAIL (a_win
                              && a_win->drawable.pScreen
                              && a_win->drawable.pScreen->visuals,
                              FALSE) ;

    visual_id = wVisual (a_win) ;
    for (i=0; i < a_win->drawable.pScreen->numVisuals; i++) {
        if (a_win->drawable.pScreen->visuals[i].vid == visual_id) {
            *a_visual = &a_win->drawable.pScreen->visuals[i] ;
            return TRUE ;
        }
    }
    return FALSE ;
}

#define NUM_WINDOW_PAIRS 256
static EphyrDRI2WindowPair window_pairs[NUM_WINDOW_PAIRS] ;

static Bool
appendWindowPairToList (WindowPtr a_local,
                        int a_remote)
{
    int i=0 ;

    EPHYR_RETURN_VAL_IF_FAIL (a_local, FALSE) ;

    EPHYR_LOG ("(local,remote):(%p, %d)\n", a_local, a_remote) ;

    for (i=0; i < NUM_WINDOW_PAIRS; i++) {
        if (window_pairs[i].local == NULL) {
            window_pairs[i].local = a_local ;
            window_pairs[i].remote = a_remote ;
            return TRUE ;
        }
    }
    return FALSE ;
}

static Bool
findWindowPairFromLocal (WindowPtr a_local,
                         EphyrDRI2WindowPair **a_pair)
{
    int i=0 ;

    EPHYR_RETURN_VAL_IF_FAIL (a_pair && a_local, FALSE) ;

    for (i=0; i < NUM_WINDOW_PAIRS; i++) {
        if (window_pairs[i].local == a_local) {
            *a_pair = &window_pairs[i] ;
#if 0
            EPHYR_LOG ("found (%p, %d)\n",
                       (*a_pair)->local,
                       (*a_pair)->remote) ;
#endif
            return TRUE ;
        }
    }
    return FALSE ;
}

Bool
findDRI2WindowPairFromRemote (int a_remote,
                          EphyrDRI2WindowPair **a_pair)
{
    int i=0 ;

    EPHYR_RETURN_VAL_IF_FAIL (a_pair, FALSE) ;

    for (i=0; i < NUM_WINDOW_PAIRS; i++) {
        if (window_pairs[i].remote == a_remote) {
            *a_pair = &window_pairs[i] ;
#if 0
            EPHYR_LOG ("found (%p, %d)\n",
                       (*a_pair)->local,
                       (*a_pair)->remote) ;
#endif
            return TRUE ;
        }
    }
    return FALSE ;
}

static Bool
createHostPeerWindow (const WindowPtr a_win,
                      int *a_peer_win)
{
    Bool is_ok=FALSE ;
    VisualPtr visual=NULL;
    EphyrBox geo ;

    EPHYR_RETURN_VAL_IF_FAIL (a_win && a_peer_win, FALSE) ;
    EPHYR_RETURN_VAL_IF_FAIL (a_win->drawable.pScreen,
                              FALSE) ;

    EPHYR_LOG ("enter. a_win '%p'\n", a_win) ;

    if (!getWindowVisual (a_win, &visual)) {
        EPHYR_LOG_ERROR ("failed to get window visual\n") ;
        goto out ;
    }
    if (!visual) {
        EPHYR_LOG_ERROR ("failed to create visual\n") ;
        goto out ;
    }

    memset (&geo, 0, sizeof (geo)) ;
    geo.x = a_win->drawable.x ;
    geo.y = a_win->drawable.y ;
    geo.width = a_win->drawable.width ;
    geo.height = a_win->drawable.height ;

    if (!hostx_create_window (a_win->drawable.pScreen->myNum,
                              &geo, visual->vid, a_peer_win)) {
        EPHYR_LOG_ERROR ("failed to create host peer window\n") ;
        goto out ;
    }
    if (!appendWindowPairToList (a_win, *a_peer_win)) {
        EPHYR_LOG_ERROR ("failed to append window to pair list\n") ;
        goto out ;
    }
    is_ok = TRUE ;
out:
    EPHYR_LOG ("leave:remote win%d\n", *a_peer_win) ;
    return is_ok ;
}

static Bool
destroyHostPeerWindow (const WindowPtr a_win)
{
    Bool is_ok = FALSE ;
    EphyrDRI2WindowPair *pair=NULL ;
    EPHYR_RETURN_VAL_IF_FAIL (a_win, FALSE) ;

    EPHYR_LOG ("enter\n") ;

    if (!findWindowPairFromLocal (a_win, &pair) || !pair) {
        EPHYR_LOG_ERROR ("failed to find peer to local window\n") ;
        goto out;
    }
    hostx_destroy_window (pair->remote) ;
    pair->local  = NULL;
    pair->remote = 0;
    is_ok = TRUE ;

out:
    EPHYR_LOG ("leave\n") ;
    return is_ok;
}

static Bool
validDrawable(ClientPtr client, XID drawable,
	      DrawablePtr *pDrawable, int *status)
{
    *status = dixLookupDrawable(pDrawable, drawable, client, 0, DixReadAccess);
    if (*status != Success) {
	client->errorValue = drawable;
	return FALSE;
    }

    return TRUE;
}

static int
ProcDRI2QueryVersion(ClientPtr client)
{
    REQUEST(xDRI2QueryVersionReq);
    xDRI2QueryVersionReply rep;
    int n;

    if (client->swapped)
	swaps(&stuff->length, n);

    REQUEST_SIZE_MATCH(xDRI2QueryVersionReq);
    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.majorVersion = 1;
    rep.minorVersion = 1;

    if (client->swapped) {
    	swaps(&rep.sequenceNumber, n);
    	swapl(&rep.length, n);
	swapl(&rep.majorVersion, n);
	swapl(&rep.minorVersion, n);
    }

    WriteToClient(client, sizeof(xDRI2QueryVersionReply), &rep);

    return client->noClientException;
}

static int
ProcDRI2Connect(ClientPtr client)
{
    REQUEST(xDRI2ConnectReq);
    xDRI2ConnectReply rep;
    DrawablePtr pDraw;
    int fd, status;
    char *driverName;
    char *deviceName;

    REQUEST_SIZE_MATCH(xDRI2ConnectReq);
    if (!validDrawable(client, stuff->window, &pDraw, &status))
	return status;

    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.driverNameLength = 0;
    rep.deviceNameLength = 0;

    if (!ephyrDRI2Connect(pDraw->pScreen,
		     stuff->driverType, &fd, &driverName, &deviceName))
	goto fail;

    rep.driverNameLength = strlen(driverName);
    rep.deviceNameLength = strlen(deviceName);
    rep.length = (rep.driverNameLength + 3) / 4 +
	    (rep.deviceNameLength + 3) / 4;

 fail:
    WriteToClient(client, sizeof(xDRI2ConnectReply), &rep);
    WriteToClient(client, rep.driverNameLength, driverName);
    WriteToClient(client, rep.deviceNameLength, deviceName);

    return client->noClientException;
}

static int
ProcDRI2Authenticate(ClientPtr client)
{
    REQUEST(xDRI2AuthenticateReq);
    xDRI2AuthenticateReply rep;
    DrawablePtr pDraw;
    int status;

    REQUEST_SIZE_MATCH(xDRI2AuthenticateReq);
    if (!validDrawable(client, stuff->window, &pDraw, &status))
	return status;

    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.length = 0;
    rep.authenticated = ephyrDRI2Authenticate(pDraw->pScreen, stuff->magic);
    WriteToClient(client, sizeof(xDRI2AuthenticateReply), &rep);

    return client->noClientException;
}

static int
ProcDRI2CreateDrawable(ClientPtr client)
{
    REQUEST(xDRI2CreateDrawableReq);
    DrawablePtr pDrawable;
    int status;

    int remote = 0;
    int remote_win = 0;
    WindowPtr window = NULL;
    EphyrDRI2WindowPair *pair = NULL ;
    EphyrDRI2WindowPrivPtr win_priv = NULL;

    REQUEST_SIZE_MATCH(xDRI2CreateDrawableReq);

    if (!validDrawable(client, stuff->drawable, &pDrawable, &status))
	return status;

    if (pDrawable->type == DRAWABLE_WINDOW) {
        window = (WindowPtr)pDrawable;
        if (findWindowPairFromLocal (window, &pair) && pair) {
            remote_win = pair->remote;
            //EPHYR_LOG ("found window '%p' paire with remote '%d'\n", window, remote_win) ;
        } else if (!createHostPeerWindow (window, &remote_win)) {
            EPHYR_LOG_ERROR ("failed to create host peer window\n") ;
            return BadAlloc ;
        }
        remote = remote_win;
    }else {
        EPHYR_LOG_ERROR ("non-drawable windows are not yet supported\n") ;
        return BadImplementation ;
    }

    status = ephyrDRI2CreateDrawable(remote);
    if (status != Success)
	return status;

    win_priv = GET_EPHYR_DRI2_WINDOW_PRIV (window) ;
    if (!win_priv) {
        win_priv = xcalloc (1, sizeof (EphyrDRI2WindowPrivRec)) ;
        if (!win_priv) {
            EPHYR_LOG_ERROR ("failed to allocate window private\n") ;
            return BadAlloc ;
        }
        dixSetPrivate(&window->devPrivates, ephyrDRI2WindowKey, win_priv);
        EPHYR_LOG ("paired window '%p' with remote '%d'\n",
                   window, remote_win) ;
    }

    if (!AddResource(stuff->drawable, dri2DrawableRes, pDrawable)) {
    	DRI2DrawableGone(pDrawable, stuff->drawable);
    	return BadAlloc;
    }

    return client->noClientException;
}

static int
ProcDRI2DestroyDrawable(ClientPtr client)
{
    REQUEST(xDRI2DestroyDrawableReq);
    DrawablePtr pDrawable;
    int status;

    REQUEST_SIZE_MATCH(xDRI2DestroyDrawableReq);
    if (!validDrawable(client, stuff->drawable, &pDrawable, &status))
	return status;

    FreeResourceByType(stuff->drawable, dri2DrawableRes, FALSE);

    return client->noClientException;
}


static void
send_buffers_reply(ClientPtr client, DrawablePtr pDrawable,
		   DRI2Buffer *buffers, int count, int width, int height)
{
    xDRI2GetBuffersReply rep;
    int skip = 0;
    int i;

    if (pDrawable->type == DRAWABLE_WINDOW) {
	for (i = 0; i < count; i++) {
	    /* Do not send the real front buffer of a window to the client.
	     */
	    if (buffers[i].attachment == DRI2BufferFrontLeft) {
		skip++;
		continue;
	    }
	}
    }

    rep.type = X_Reply;
    rep.length = (count - skip) * sizeof(xDRI2Buffer) / 4;
    rep.sequenceNumber = client->sequence;
    rep.width = width;
    rep.height = height;
    rep.count = count - skip;
    WriteToClient(client, sizeof(xDRI2GetBuffersReply), &rep);

    for (i = 0; i < count; i++) {
		xDRI2Buffer buffer;

		/* Do not send the real front buffer of a window to the client.
		 */
		if ((pDrawable->type == DRAWABLE_WINDOW)
			&& (buffers[i].attachment == DRI2BufferFrontLeft)) {
			continue;
		}

		buffer.attachment = buffers[i].attachment;
		buffer.name = buffers[i].name;
		buffer.pitch = buffers[i].pitch;
		buffer.cpp = buffers[i].cpp;
		buffer.flags = buffers[i].flags;
		WriteToClient(client, sizeof(xDRI2Buffer), &buffer);
    }
}


static int
ProcDRI2GetBuffers(ClientPtr client)
{
    REQUEST(xDRI2GetBuffersReq);
    DrawablePtr pDrawable;
    DRI2Buffer *buffers;
    int status, width, height, count;
    unsigned int *attachments;

    WindowPtr window=NULL;
    EphyrDRI2WindowPair *pair=NULL;

    REQUEST_FIXED_SIZE(xDRI2GetBuffersReq, stuff->count * 4);
    if (!validDrawable(client, stuff->drawable, &pDrawable, &status))
	return status;

    window = (WindowPtr)pDrawable ;
    memset (&pair, 0, sizeof (pair)) ;
    if (!findWindowPairFromLocal (window, &pair) || !pair) {
        EPHYR_LOG_ERROR ("failed to find remote peer drawable\n") ;
        return BadMatch ;
    }

    attachments = (unsigned int *) &stuff[1];
    buffers = ephyrDRI2GetBuffers(pair->remote, &width, &height,
			     attachments, stuff->count, &count);

    if (buffers){
        send_buffers_reply(client, pDrawable, buffers, count, width, height);
    }

    return client->noClientException;
}

static int
ProcDRI2GetBuffersWithFormat(ClientPtr client)
{
    REQUEST(xDRI2GetBuffersReq);
    DrawablePtr pDrawable;
    DRI2Buffer *buffers;
    int status, width, height, count;
    unsigned int *attachments;

    WindowPtr window=NULL;
    EphyrDRI2WindowPair *pair=NULL;

    REQUEST_FIXED_SIZE(xDRI2GetBuffersReq, stuff->count * (2 * 4));
    if (!validDrawable(client, stuff->drawable, &pDrawable, &status))
	return status;

    window = (WindowPtr)pDrawable ;
    memset (&pair, 0, sizeof (pair)) ;
    if (!findWindowPairFromLocal (window, &pair) || !pair) {
        EPHYR_LOG_ERROR ("failed to find remote peer drawable\n") ;
        return BadMatch ;
    }

    attachments = (unsigned int *) &stuff[1];
    buffers = ephyrDRI2GetBuffersWithFormat(pair->remote, &width, &height,
				       attachments, stuff->count, &count);

    if (buffers) {
        send_buffers_reply(client, pDrawable, buffers, count, width, height);
    }

    return client->noClientException;
}

static int
ProcDRI2CopyRegion(ClientPtr client)
{
    REQUEST(xDRI2CopyRegionReq);
    xDRI2CopyRegionReply rep;
    DrawablePtr pDrawable;
    int status;
    RegionPtr pRegion;

    WindowPtr window=NULL;
    EphyrDRI2WindowPair *pair=NULL;

    REQUEST_SIZE_MATCH(xDRI2CopyRegionReq);

    if (!validDrawable(client, stuff->drawable, &pDrawable, &status))
	return status;

    window = (WindowPtr)pDrawable ;
    memset (&pair, 0, sizeof (pair)) ;
    if (!findWindowPairFromLocal (window, &pair) || !pair) {
        EPHYR_LOG_ERROR ("failed to find remote peer drawable\n") ;
        return BadMatch ;
    }

    VERIFY_REGION(pRegion, stuff->region, client, DixReadAccess);
    status = ephyrDRI2CopyRegion(stuff->drawable, pair, pRegion, stuff->dest, stuff->src);
    if (status != Success)
	return status;

    hostx_copy_region(stuff->drawable, pair, pRegion, pDrawable->x, pDrawable->y);

    /* CopyRegion needs to be a round trip to make sure the X server
     * queues the swap buffer rendering commands before the DRI client
     * continues rendering.  The reply has a bitmask to signal the
     * presense of optional return values as well, but we're not using
     * that yet.
     */

    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;

    WriteToClient(client, sizeof(xDRI2CopyRegionReply), &rep);

    return client->noClientException;
}

static int
ProcDRI2Dispatch (ClientPtr client)
{
    REQUEST(xReq);

    switch (stuff->data) {
    case X_DRI2QueryVersion:
	return ProcDRI2QueryVersion(client);
    }

    if (!LocalClient(client))
	return BadRequest;

    switch (stuff->data) {
    case X_DRI2Connect:
	return ProcDRI2Connect(client);
    case X_DRI2Authenticate:
	return ProcDRI2Authenticate(client);
    case X_DRI2CreateDrawable:
	return ProcDRI2CreateDrawable(client);
    case X_DRI2DestroyDrawable:
	return ProcDRI2DestroyDrawable(client);
    case X_DRI2GetBuffers:
	return ProcDRI2GetBuffers(client);
    case X_DRI2CopyRegion:
	return ProcDRI2CopyRegion(client);
    case X_DRI2GetBuffersWithFormat:
	return ProcDRI2GetBuffersWithFormat(client);
    default:
		{
			fprintf(stderr, "Bad Request from client\n");
			return BadRequest;
		}
    }
}

static int
SProcDRI2Connect(ClientPtr client)
{
    REQUEST(xDRI2ConnectReq);
    xDRI2ConnectReply rep;
    int n;

    /* If the client is swapped, it's not local.  Talk to the hand. */

    swaps(&stuff->length, n);
    if (sizeof(*stuff) / 4 != client->req_len)
	return BadLength;

    rep.sequenceNumber = client->sequence;
    swaps(&rep.sequenceNumber, n);
    rep.length = 0;
    rep.driverNameLength = 0;
    rep.deviceNameLength = 0;

    return client->noClientException;
}

static int
SProcDRI2Dispatch (ClientPtr client)
{
    REQUEST(xReq);

    /*
     * Only local clients are allowed DRI access, but remote clients
     * still need these requests to find out cleanly.
     */
    switch (stuff->data)
    {
    case X_DRI2QueryVersion:
	return ProcDRI2QueryVersion(client);
    case X_DRI2Connect:
	return SProcDRI2Connect(client);
    default:
	return BadRequest;
    }
}
