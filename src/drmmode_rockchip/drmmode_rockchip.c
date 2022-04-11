#include "../drmmode_driver.h"
#include <stddef.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <sys/ioctl.h>

#include "xf86.h"

#include "rockchip_drm.h"

/* Cursor dimensions
 * Technically we probably don't have any size limit.. since we
 * are just using an overlay... but xserver will always create
 * cursor images in the max size, so don't use width/height values
 * that are too big
 */
#define CURSORW  (64)
#define CURSORH  (64)

/*
 * Padding added down each side of cursor image. This is a workaround for a bug
 * causing corruption when the cursor reaches the screen edges.
 */
#define CURSORPAD (16)

#define ALIGN(val, align)	(((val) + (align) - 1) & ~((align) - 1))

static int init_plane_for_cursor(int drm_fd, uint32_t plane_id)
{
    return 0;
}

static int create_custom_gem(int fd, struct armsoc_create_gem *create_gem)
{
	struct drm_rockchip_gem_create create_rockchip;
	int ret;
	unsigned int pitch;

	/* make pitch a multiple of 64 bytes for best performance */
	pitch = ALIGN(create_gem->width * ((create_gem->bpp + 7) / 8), 64);
	memset(&create_rockchip, 0, sizeof(create_rockchip));
	create_rockchip.size = create_gem->height * pitch;

	assert((create_gem->buf_type == ARMSOC_BO_SCANOUT) ||
			(create_gem->buf_type == ARMSOC_BO_NON_SCANOUT));

	create_rockchip.flags = ROCKCHIP_BO_WC;

	ret = drmIoctl(fd, DRM_IOCTL_ROCKCHIP_GEM_CREATE, &create_rockchip);
	if (ret)
		return ret;

	/* Convert custom create_rockchip to generic create_gem */
	create_gem->handle = create_rockchip.handle;
	create_gem->pitch = pitch;
	create_gem->size = create_rockchip.size;

	return 0;
}

struct drmmode_interface rockchip_interface = {
	"rockchip"	      /* name of drm driver */,
	1                     /* use_page_flip_events */,
	0                     /* use_early_display */,
	CURSORW               /* cursor width */,
	CURSORH               /* cursor_height */,
	CURSORPAD             /* cursor padding */,
	HWCURSOR_API_PLANE    /* cursor_api */,
	init_plane_for_cursor /* init_plane_for_cursor */,
	1                     /* vblank_query_supported */,
	create_custom_gem     /* create_custom_gem */,
};
