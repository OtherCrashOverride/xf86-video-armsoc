/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2011 Texas Instruments, Inc
 * Copyright © 2017 OtherCrashOverride
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <rob@ti.com>
 *    OtherCrashOverride <OtherCrashOverride@noreply.user.github.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "armsoc_driver.h"
#include "armsoc_exa.h"

#include "exa.h"

/* Exynose G2D */
#define __user 
#include <uapi/drm/drm.h>
#include <uapi/drm/exynos_drm.h>
#include "exynos_fimg2d.h"

/* This file has a trivial EXA implementation which accelerates nothing.  It
 * is used as the fall-back in case the EXA implementation for the current
 * chipset is not available.  (For example, on chipsets which used the closed
 * source IMG PowerVR EXA implementation, if the closed-source submodule is
 * not installed.
 */

struct ARMSOCNullEXARec {
	struct ARMSOCEXARec base;
	ExaDriverPtr exa;
	/* add any other driver private data here.. */
	struct g2d_context* ctx;
	PixmapPtr pSource;
	int xdir;
	int ydir;
};

static Bool
PrepareSolidFail(PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fill_colour)
{
	return FALSE;
}

static Bool
PrepareCopy(PixmapPtr pSrc, PixmapPtr pDst, int xdir, int ydir,
		int alu, Pixel planemask)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pDst->drawable.pScreen);
	struct ARMSOCRec* pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCNullEXARec* nullExaRec = (struct ARMSOCNullEXARec*)pARMSOC->pARMSOCEXA;

	struct ARMSOCPixmapPrivRec* srcPriv = exaGetPixmapDriverPrivate(pSrc);
	struct ARMSOCPixmapPrivRec* dstPriv = exaGetPixmapDriverPrivate(pDst);

	uint32_t srcBpp;
	uint32_t dstBpp;


	// If there are no buffer objects, fallback
	if (!srcPriv->bo || !dstPriv->bo)
	{
		return FALSE;
	}

	// If bpp is not 32 or 16, fallback
	srcBpp = armsoc_bo_bpp(srcPriv->bo);
	dstBpp = armsoc_bo_bpp(dstPriv->bo);

	if (((srcBpp != 32) && (srcBpp != 16)) ||
		((dstBpp != 32) && (dstBpp != 16)))
	{
		return FALSE;
	}


	// Save required information for later
	nullExaRec->pSource = pSrc;
	nullExaRec->xdir = xdir;
	nullExaRec->ydir = ydir;


	return TRUE;
}

static void Copy(PixmapPtr pDstPixmap, int srcX, int srcY, int dstX, int dstY,
	int width, int height)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
	struct ARMSOCRec* pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCNullEXARec* nullExaRec = (struct ARMSOCNullEXARec*)pARMSOC->pARMSOCEXA;

	struct ARMSOCPixmapPrivRec* srcPriv = exaGetPixmapDriverPrivate(nullExaRec->pSource);
	struct ARMSOCPixmapPrivRec* dstPriv = exaGetPixmapDriverPrivate(pDstPixmap);

	struct g2d_image srcImage;
	struct g2d_image dstImage;
	int ret;


	memset(&srcImage, 0, sizeof(srcImage));
	memset(&dstImage, 0, sizeof(dstImage));


	// Source
	switch (armsoc_bo_bpp(srcPriv->bo))
	{
	case 32:
		srcImage.color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
		break;

	case 16:
		srcImage.color_mode = G2D_COLOR_FMT_RGB565;
		break;

	default:
		// Not supported
		ERROR_MSG("EXA Copy: srcImage bpp not supported. (%d)", armsoc_bo_bpp(srcPriv->bo));
		break;
	}

	//srcImage.color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
	srcImage.width = armsoc_bo_width(srcPriv->bo);
	srcImage.height = armsoc_bo_height(srcPriv->bo);
	srcImage.stride = armsoc_bo_pitch(srcPriv->bo);
	srcImage.x_dir = (nullExaRec->xdir < 1);
	srcImage.y_dir = (nullExaRec->ydir < 1);

	srcImage.buf_type = G2D_IMGBUF_GEM;
	srcImage.bo[0] = armsoc_bo_handle(srcPriv->bo);


	// Destination
	switch (armsoc_bo_bpp(dstPriv->bo))
	{
	case 32:
		dstImage.color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
		break;

	case 16:
		dstImage.color_mode = G2D_COLOR_FMT_RGB565;
		break;

	default:
		// Not supported
		ERROR_MSG("EXA Copy: dstImage bpp not supported. (%d)", armsoc_bo_bpp(dstPriv->bo));
		break;
	}

	//dstImage.color_mode = G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB;
	dstImage.width = armsoc_bo_width(dstPriv->bo);
	dstImage.height = armsoc_bo_height(dstPriv->bo);
	dstImage.stride = armsoc_bo_pitch(dstPriv->bo);
	dstImage.x_dir = (nullExaRec->xdir < 1);
	dstImage.y_dir = (nullExaRec->ydir < 1);

	dstImage.buf_type = G2D_IMGBUF_GEM;
	dstImage.bo[0] = armsoc_bo_handle(dstPriv->bo);


	// Copy
	ret = g2d_copy(nullExaRec->ctx, &srcImage, &dstImage, srcX, srcY, dstX, dstY, width, height);
	if (ret < 0)
	{
		//xf86DrvMsg(-1, X_ERROR, "g2d_copy: srcX=%d, srcY=%d, dstX=%d, dstY=%d, width=%d, height=%d | src_bpp=%d, dst_bpp=%d (ret=%d)\n",
		//	srcX, srcY, dstX, dstY, width, height, armsoc_bo_bpp(srcPriv->bo), armsoc_bo_bpp(dstPriv->bo), ret);

		xf86DrvMsg(-1, X_ERROR, "g2d_copy: srcX=%d, srcY=%d, dstX=%d, dstY=%d, width=%d, height=%d | "
			"src_width=%d, src_height=%d src_stride=%d src_xdir=%d src_ydir=%d | "
			"dst_width=%d, dst_height=%d dst_stride=%d dst_xdir=%d dst_ydir=%d | "
			"(ret=%d)\n",
			srcX, srcY, dstX, dstY, width, height, 
			srcImage.width, srcImage.height, srcImage.stride, srcImage.x_dir, srcImage.y_dir,
			dstImage.width, dstImage.height, dstImage.stride, dstImage.x_dir, dstImage.y_dir,
			ret);
	}

	//ret = g2d_exec(nullExaRec->ctx);
}

static void DoneCopy(PixmapPtr pDstPixmap)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pDstPixmap->drawable.pScreen);
	struct ARMSOCRec* pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCNullEXARec* nullExaRec = (struct ARMSOCNullEXARec*)pARMSOC->pARMSOCEXA;

	g2d_exec(nullExaRec->ctx);
}

static Bool
CheckCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
		PicturePtr pDstPicture)
{
	return FALSE;
}

static Bool
PrepareCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
		PicturePtr pDstPicture, PixmapPtr pSrc,
		PixmapPtr pMask, PixmapPtr pDst)
{
	return FALSE;
}

/**
 * CloseScreen() is called at the end of each server generation and
 * cleans up everything initialised in InitNullEXA()
 */
static Bool
CloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	exaDriverFini(pScreen);
	free(((struct ARMSOCNullEXARec *)pARMSOC->pARMSOCEXA)->exa);
	free(pARMSOC->pARMSOCEXA);
	pARMSOC->pARMSOCEXA = NULL;

	return TRUE;
}

/* FreeScreen() is called on an error during PreInit and
 * should clean up anything initialised before InitNullEXA()
 * (which currently is nothing)
 *
 */
static void
FreeScreen(FREE_SCREEN_ARGS_DECL)
{
}

struct ARMSOCEXARec *
InitNullEXA(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd)
{
	struct ARMSOCNullEXARec *null_exa;
	struct ARMSOCEXARec *armsoc_exa;
	ExaDriverPtr exa;

	INFO_MSG("Soft EXA mode");

	null_exa = calloc(1, sizeof(*null_exa));
	if (!null_exa)
		goto out;

	armsoc_exa = (struct ARMSOCEXARec *)null_exa;

	exa = exaDriverAlloc();
	if (!exa)
		goto free_null_exa;

	null_exa->exa = exa;

	exa->exa_major = EXA_VERSION_MAJOR;
	exa->exa_minor = EXA_VERSION_MINOR;

	exa->pixmapOffsetAlign = 0;
	exa->pixmapPitchAlign = 32;
	exa->flags = EXA_OFFSCREEN_PIXMAPS |
			EXA_HANDLES_PIXMAPS | EXA_SUPPORTS_PREPARE_AUX;
	exa->maxX = 4096;
	exa->maxY = 4096;

	/* Required EXA functions: */
	exa->WaitMarker = ARMSOCWaitMarker;
	exa->CreatePixmap2 = ARMSOCCreatePixmap2;
	exa->DestroyPixmap = ARMSOCDestroyPixmap;
	exa->ModifyPixmapHeader = ARMSOCModifyPixmapHeader;

	exa->PrepareAccess = ARMSOCPrepareAccess;
	exa->FinishAccess = ARMSOCFinishAccess;
	exa->PixmapIsOffscreen = ARMSOCPixmapIsOffscreen;

	/* Always fallback for software operations */
	//exa->PrepareCopy = PrepareCopyFail;
	exa->PrepareSolid = PrepareSolidFail;
	exa->CheckComposite = CheckCompositeFail;
	exa->PrepareComposite = PrepareCompositeFail;

	exa->PrepareCopy = PrepareCopy;
	exa->Copy = Copy;
	exa->DoneCopy = DoneCopy;

	if (!exaDriverInit(pScreen, exa)) {
		ERROR_MSG("exaDriverInit failed");
		goto free_exa;
	}

	armsoc_exa->CloseScreen = CloseScreen;
	armsoc_exa->FreeScreen = FreeScreen;


	// Initialize a G2D context
	//INFO_MSG("G2D Initializing.");

	null_exa->ctx = g2d_init(fd);
	if (!null_exa->ctx) {
		ERROR_MSG("exaDriverInit g2d_init failed");
		goto free_exa;
	}

	INFO_MSG("G2D Initialized.");


	return armsoc_exa;

free_exa:
	free(exa);
free_null_exa:
	free(null_exa);
out:
	return NULL;
}

