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

#include <rga/RgaApi.h>
#include <rga/im2d.h>
#include <rga/rga.h>
#include <pixman-1/pixman.h>

/* This file has an EXA implementation using the Rockchip RGA accelerator. */

struct ARMSOCNullEXARec {
	struct ARMSOCEXARec base;
	ExaDriverPtr exa;
	/* add any other driver private data here.. */
    int display_fd;
	PixmapPtr pSource;
	int xdir;
	int ydir;
	uint32_t fillColor;
};


#if 0
/* graphics functions, as in GC.alu */

#define GXclear                 0x0             /* 0 */
#define GXand                   0x1             /* src AND dst */
#define GXandReverse            0x2             /* src AND NOT dst */
#define GXcopy                  0x3             /* src */
#define GXandInverted           0x4             /* NOT src AND dst */
#define GXnoop                  0x5             /* dst */
#define GXxor                   0x6             /* src XOR dst */
#define GXor                    0x7             /* src OR dst */
#define GXnor                   0x8             /* NOT src AND NOT dst */
#define GXequiv                 0x9             /* NOT src XOR dst */
#define GXinvert                0xa             /* NOT dst */
#define GXorReverse             0xb             /* src OR NOT dst */
#define GXcopyInverted          0xc             /* NOT src */
#define GXorInverted            0xd             /* NOT src OR dst */
#define GXnand                  0xe             /* NOT src OR NOT dst */
#define GXset                   0xf             /* 1 */
#endif

/*
* The alu raster op is one of the GX*
* graphics functions listed in X.h
*/
static Bool
PrepareSolid(PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fill_color)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
	struct ARMSOCRec* pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCNullEXARec* nullExaRec = (struct ARMSOCNullEXARec*)pARMSOC->pARMSOCEXA;
	struct ARMSOCPixmapPrivRec* dstPriv = exaGetPixmapDriverPrivate(pPixmap);
	uint32_t dstBpp;

	if (pARMSOC->NoRGA)
	{
		return FALSE;
	}

	// Only GXset operation is supported
	if (alu != GXset)
	{
		return FALSE;
	}

	// If there are no buffer objects, fallback
	if (!dstPriv->bo)
	{
		return FALSE;
	}

	// If bpp is not 32 or 16, fallback
	dstBpp = armsoc_bo_bpp(dstPriv->bo);
	if (((dstBpp != 32) && (dstBpp != 16)))
	{
		return FALSE;
	}

	// Save required information for later
	nullExaRec->fillColor = (uint32_t)fill_color;

	return TRUE;
}

static void
Solid(PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pPixmap->drawable.pScreen);
	struct ARMSOCRec* pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCNullEXARec* nullExaRec = (struct ARMSOCNullEXARec*)pARMSOC->pARMSOCEXA;
	struct ARMSOCPixmapPrivRec* dstPriv = exaGetPixmapDriverPrivate(pPixmap);

    rga_info_t dst;
    int width;
    int format;
    int ret;

    memset(&dst, 0, sizeof(dst));

	switch (armsoc_bo_depth(dstPriv->bo))
	{
	case 32:
        width = armsoc_bo_pitch(dstPriv->bo) / 4;
        format = RK_FORMAT_RGBA_8888;
		break;

	case 24:
        width = armsoc_bo_pitch(dstPriv->bo) / 4;
        format = RK_FORMAT_RGBX_8888;
		break;

	case 16:
        width = armsoc_bo_pitch(dstPriv->bo) / 2;
        format = RK_FORMAT_RGB_565;
		break;

	default:
		// Not supported
		ERROR_MSG("EXA Solid: dstImage bpp not supported. (%d)", armsoc_bo_bpp(dstPriv->bo));
		return;
	}


    dst.virAddr = armsoc_bo_map(dstPriv->bo);
    dst.rect.xoffset = x1;
    dst.rect.yoffset = y1;
    dst.rect.width = x2 - x1;
    dst.rect.height = y2 - y1;
    dst.rect.wstride = width;
    dst.rect.hstride = armsoc_bo_height(dstPriv->bo);
    dst.rect.format = format;
    dst.color = nullExaRec->fillColor;

    ret = c_RkRgaColorFill(&dst);
    if (ret)
    {
        ERROR_MSG("c_RkRgaColorFill failed.\n");        
    }
}

static void
DoneSolid(PixmapPtr pPixmap)
{
	/* Nothing to do. */
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

	if (pARMSOC->NoRGA)
	{
		return FALSE;
	}
	
	// Only GXcopy operation is supported
	if (alu != GXcopy)
	{
		return FALSE;
	}

    // if (xdir != 1 || ydir != 1)
    // {
    //     return FALSE;
    // }

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

    rga_info_t dst;
    rga_info_t src;
    int ret;
    int src_width = 0;
    int src_format = 0;
    int dst_width = 0;
    int dst_format = 0;
	pixman_image_t * srcPix;
	pixman_image_t * dstPix;
	pixman_format_code_t srcPixFormat;
	pixman_format_code_t dstPixFormat;


    memset(&dst, 0, sizeof(dst));
    memset(&src, 0, sizeof(src));

	switch (armsoc_bo_depth(srcPriv->bo))
	{
	case 32:
        src_width =  armsoc_bo_pitch(srcPriv->bo) / 4;
        src_format = RK_FORMAT_RGBA_8888;
        srcPixFormat = PIXMAN_a8b8g8r8;
		break;

	case 24:
        src_width = armsoc_bo_pitch(srcPriv->bo) / 4;
        src_format = RK_FORMAT_RGBX_8888;
        srcPixFormat = PIXMAN_x8b8g8r8;
		break;

	case 16:
        src_width = armsoc_bo_pitch(srcPriv->bo) / 2;
        src_format = RK_FORMAT_RGB_565;
        srcPixFormat = PIXMAN_r5g6b5;
		break;

	default:
		// Not supported
		ERROR_MSG("EXA Solid: srcImage bpp not supported. (%d)", armsoc_bo_bpp(srcPriv->bo));
		return;
	}

	switch (armsoc_bo_depth(dstPriv->bo))
	{
	case 32:
        dst_width = armsoc_bo_pitch(dstPriv->bo) / 4;
        dst_format = RK_FORMAT_RGBA_8888;
        dstPixFormat = PIXMAN_a8b8g8r8;
		break;

	case 24:
        dst_width = armsoc_bo_pitch(dstPriv->bo) / 4;
        dst_format = RK_FORMAT_RGBX_8888;
        dstPixFormat = PIXMAN_x8b8g8r8;
		break;

	case 16:
        dst_width = armsoc_bo_pitch(dstPriv->bo) / 2;
        dst_format = RK_FORMAT_RGB_565;
        dstPixFormat = PIXMAN_r5g6b5;
		break;

	default:
		// Not supported
		ERROR_MSG("EXA Solid: dstImage bpp not supported. (%d)", armsoc_bo_bpp(dstPriv->bo));
		return;
	}

    //ERROR_MSG("EXA Solid: sw=%d, sf=%d, dw=%d, df=%d", src_width, src_format, dst_width, dst_format);

    if (width < 128 || height < 128)
    {
        ARMSOCPrepareAccess(pDstPixmap, EXA_PREPARE_DEST);
        ARMSOCPrepareAccess(nullExaRec->pSource, EXA_PREPARE_SRC);


        srcPix = pixman_image_create_bits(srcPixFormat,
            armsoc_bo_width(srcPriv->bo), armsoc_bo_height(srcPriv->bo),
            (uint32_t*)armsoc_bo_map(srcPriv->bo),
            armsoc_bo_pitch(srcPriv->bo));

        dstPix = pixman_image_create_bits(dstPixFormat,
            armsoc_bo_width(dstPriv->bo), armsoc_bo_height(dstPriv->bo),
            (uint32_t*)armsoc_bo_map(dstPriv->bo),
            armsoc_bo_pitch(dstPriv->bo));

        pixman_image_composite(PIXMAN_OP_SRC,
					       srcPix,
					       NULL,
					       dstPix,
					       srcX,
					       srcY,
					       0,
					       0,
					       dstX,
					       dstY,
					       width,
					       height);

        pixman_image_unref(srcPix);
        pixman_image_unref(dstPix);

        ARMSOCFinishAccess(nullExaRec->pSource, EXA_PREPARE_SRC);
        ARMSOCFinishAccess(pDstPixmap, EXA_PREPARE_DEST);

        return;
    }


    dst.virAddr = armsoc_bo_map(dstPriv->bo);
    dst.rect.xoffset = dstX;
    dst.rect.yoffset = dstY;
    dst.rect.width = width;
    dst.rect.height = height;
    dst.rect.wstride = dst_width;
    dst.rect.hstride = armsoc_bo_height(dstPriv->bo);
    dst.rect.format = dst_format;

    src.virAddr = armsoc_bo_map(srcPriv->bo);
    src.rect.xoffset = srcX;
    src.rect.yoffset = srcY;
    src.rect.width = width;
    src.rect.height = height;
    src.rect.wstride = src_width;
    src.rect.hstride = armsoc_bo_height(srcPriv->bo);
    src.rect.format = src_format;

    ret = c_RkRgaBlit(&src, &dst, NULL);
    if (ret)
    {
        ERROR_MSG("c_RkRgaBlit failed.");
        
        ERROR_MSG("blit: sx=%d, sy=%d, sw=%d, sh=%d, sws=%d, shs=%d",
            src.rect.xoffset, src.rect.yoffset,
            src.rect.width, src.rect.height,
            src.rect.wstride, src.rect.hstride);

        ERROR_MSG("blit: dx=%d, dy=%d, dw=%d, dh=%d, dws=%d, dhs=%d",
            dst.rect.xoffset, dst.rect.yoffset,
            dst.rect.width, dst.rect.height,
            dst.rect.wstride, dst.rect.hstride);
    }
}

static void DoneCopy(PixmapPtr pDstPixmap)
{
	/* Nothing to do. */
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
	//struct ARMSOCNullEXARec* nullExaRec = (struct ARMSOCNullEXARec*)pARMSOC->pARMSOCEXA;


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

	INFO_MSG("Rockchip RGA EXA mode");

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
	//exa->PrepareSolid = PrepareSolidFail;
	exa->CheckComposite = CheckCompositeFail;
	exa->PrepareComposite = PrepareCompositeFail;

	exa->PrepareCopy = PrepareCopy;
	exa->Copy = Copy;
	exa->DoneCopy = DoneCopy;

	exa->PrepareSolid = PrepareSolid;
	exa->Solid = Solid;
	exa->DoneSolid = DoneSolid;

	if (!exaDriverInit(pScreen, exa)) {
		ERROR_MSG("exaDriverInit failed");
		goto free_exa;
	}

	armsoc_exa->CloseScreen = CloseScreen;
	armsoc_exa->FreeScreen = FreeScreen;


	// Initialize RGA
    null_exa->display_fd = fd;
	//c_RkRgaInit();

	return armsoc_exa;

free_exa:
	free(exa);
free_null_exa:
	free(null_exa);
out:
	return NULL;
}

