/*
 * Copyright (c) 2016-2017 Renesas Electronics Corporation
 * Released under the MIT license
 * http://opensource.org/licenses/mit-license.php
 */

/******************************************************************************
 *  link state  : rpf -> bru -> wpf
 *  memory type : mmap / userptr / dmabuf
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <mediactl/mediactl.h>
#include <mediactl/v4l2subdev.h>

#include "mmngr_user_public.h"
#include "mmngr_buf_user_public.h"

/******************************************************************************
 *  macros
 ******************************************************************************/
/* device name */
#ifndef USE_M3
/* for h3 */
#define MEDIA_DEV_NAME		"/dev/media2"	/* fe960000.vsp */
#else
/* for m3 */
#define MEDIA_DEV_NAME		"/dev/media1"	/* fe960000.vsp */
#endif

#define SRC1_INPUT_DEV		"%s rpf.0 input"
#define SRC2_INPUT_DEV		"%s rpf.1 input"
#define DST_OUTPUT_DEV		"%s wpf.0 output"

/* source parameter */
#define SRC1_FILENAME		"1280_720_ARGB32.argb"
#define SRC1_WIDTH		(1280)		/* src1: width  */
#define SRC1_HEIGHT		(720)		/* src1: height */
#define SRC1_SIZE		(SRC1_WIDTH*SRC1_HEIGHT*4)

#define SRC2_WIDTH		(640)		/* src2: width  */
#define SRC2_HEIGHT		(480)		/* src2: height */
#define SRC2_SIZE		(SRC2_WIDTH*SRC2_HEIGHT*4)

/* destination parameter */
#define DST_FILENAME_MMAP	"1280_720_ARGB32_BRU_MMAP.argb"
#define DST_FILENAME_USERPTR	"1280_720_ARGB32_BRU_USERPTR.argb"
#define DST_FILENAME_DMABUF	"1280_720_ARGB32_BRU_DMABUF.argb"
#define DST_WIDTH		(1280)		/* dst: width  */
#define DST_HEIGHT		(720)		/* dst: height */
#define DST_SIZE		(DST_WIDTH*DST_HEIGHT*4)

/******************************************************************************
 *  structure
 ******************************************************************************/
struct dev_pads {
	struct media_pad	*ppad0;
	struct media_pad	*ppad1;
};
struct dev_brupads {
	struct media_pad	*ppad0;
	struct media_pad	*ppad1;
	struct media_pad	*ppad2;
	struct media_pad	*ppad3;
	struct media_pad	*ppad4;
	struct media_pad	*ppad5;
};

/******************************************************************************
 *  internal function
 ******************************************************************************/
static int	test_bru_mmap(void);
static int	test_bru_userptr(void);
static int	test_bru_dmabuf(void);

static int	read_file(unsigned char*, unsigned int, const char*);
static int	write_file(unsigned char*, unsigned int, const char*);

static int	call_media_ctl(struct media_device **, const char **);

static void	make_stripe_image(void *pbuf, int width, int height);
static void	make_color(unsigned int *ptr, unsigned int color, int count);
static void	calc_img_premultiplied_alpha(void *pbuf, int width, int height);

static int	open_video_device(struct media_device *pmedia,
				  char *pentity_base, const char *pmedia_name);

/******************************************************************************
 *  main
 ******************************************************************************/
void print_usage(const char *pname)
{
	printf("----------------------------------\n");
#ifndef USE_M3
	printf(" exec for H3 settings\n");
#else
	printf(" exec for M3 settings\n");
#endif
	printf("----------------------------------\n");
	printf(" Usage : %s [option]\n", pname);
	printf("    option\n");
	printf("        -m: use MMAP [default]\n");
	printf("        -u: use USERPTR\n");
	printf("        -d: use DMABUF\n");
	printf("        -h: print usage\n");
	printf("----------------------------------\n");
}

int main(int argc, char *argv[])
{
	int opt;

	opt = getopt(argc, argv, "mudh");

	switch (opt) {
	case 'm':
		printf("exec MMAP\n");
		test_bru_mmap();
		break;
	case 'u':
		printf("exec USERPTR\n");
		test_bru_userptr();
		break;
	case 'd':
		printf("exec DMABUF\n");
		test_bru_dmabuf();
		break;
	case 'h':
		print_usage(argv[0]);
		break;
	default:
		print_usage(argv[0]);
		printf("exec MMAP\n");
		test_bru_mmap();
		break;
	}

	exit(0);
}

/******************************************************************************
 *  mmap
 ******************************************************************************/
static int test_bru_mmap(void)
{
	struct media_device  *pmedia;

	unsigned char  *psrc1_buf;
	unsigned char  *psrc2_buf;
	unsigned char  *pdst_buf;

	int src1_fd  = -1;	/* src file descriptor */
	int src2_fd  = -1;	/* src file descriptor */
	int dst_fd   = -1;	/* dst file descriptor */

	unsigned int  type;
	int           ret = -1;

	struct v4l2_format          fmt;
	struct v4l2_requestbuffers  req_buf;
	struct v4l2_buffer          buf;
	struct v4l2_capability      cap;
	struct v4l2_plane           planes[VIDEO_MAX_PLANES];

	unsigned int        caps;
	struct v4l2_format  gfmt;

	const char *pmedia_name;

	/*-------------------------------------------------------------------*/
	/*  Call media-ctl                                                   */
	/*-------------------------------------------------------------------*/
	ret = call_media_ctl(&pmedia, &pmedia_name);
	if (ret < 0) {
		printf("Error : media-ctl call failed.\n");
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Open device                                                      */
	/*-------------------------------------------------------------------*/
	/* src device(rpf.0) */
	src1_fd = open_video_device(pmedia, SRC1_INPUT_DEV, pmedia_name);
	if (src1_fd == -1) {
		printf("Error open src1 device: %s (%d).\n",
			strerror(errno), errno);
		return -1;
	}

	/* src device(rpf.1) */
	src2_fd = open_video_device(pmedia, SRC2_INPUT_DEV, pmedia_name);
	if (src2_fd == -1) {
		printf("Error open src2 device: %s (%d).\n",
			strerror(errno), errno);
		return -1;
	}

	/* dst device(wpf.0) */
	dst_fd = open_video_device(pmedia, DST_OUTPUT_DEV, pmedia_name);
	if (dst_fd == -1) {
		printf("Error open dst device: %s (%d).\n",
			strerror(errno), errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QUERYCAP                                                  */
	/*-------------------------------------------------------------------*/
	/* src1 */
	memset(&cap, 0, sizeof(cap));
	ret = ioctl(src1_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}
	caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
	     ? cap.device_caps : cap.capabilities;

	if ((caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE) == 0) {
		printf("Device does not have required capabilitiy. line=%d\n",
			__LINE__);
		return -1;
	}

	/* src2 */
	memset(&cap, 0, sizeof(cap));
	ret = ioctl(src2_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}
	caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
	     ? cap.device_caps : cap.capabilities;

	if ((caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE) == 0) {
		printf("Device does not have required capabilitiy. line=%d\n",
			__LINE__);
		return -1;
	}

	/* dst */
	ret = ioctl(dst_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}
	caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
	     ? cap.device_caps : cap.capabilities;

	if ((caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0) {
		printf("Device does not have required capabilitiy. line=%d\n",
			__LINE__);
		return -1;
	}

	/*********************************************************************
	 *  src1
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_S_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&fmt, 0, sizeof(fmt));
	fmt.type                    = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.width        = SRC1_WIDTH;
	fmt.fmt.pix_mp.height       = SRC1_HEIGHT;
	fmt.fmt.pix_mp.field        = V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.pixelformat  = V4L2_PIX_FMT_ARGB32;
	fmt.fmt.pix_mp.num_planes   = 1;
	fmt.fmt.pix_mp.plane_fmt[0].bytesperline  = 0;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage     = 0;

	ret = ioctl(src1_fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_G_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&gfmt, 0x00, sizeof(gfmt));
	gfmt.type = fmt.type;
	ret = ioctl(src1_fd, VIDIOC_G_FMT, &gfmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	if ((fmt.fmt.pix_mp.width        != gfmt.fmt.pix_mp.width)       ||
	    (fmt.fmt.pix_mp.height       != gfmt.fmt.pix_mp.height)      ||
	    (fmt.fmt.pix_mp.field        != gfmt.fmt.pix_mp.field)       ||
	    (fmt.fmt.pix_mp.pixelformat  != gfmt.fmt.pix_mp.pixelformat) ||
	    (fmt.fmt.pix_mp.num_planes   != gfmt.fmt.pix_mp.num_planes)  ||
	    (fmt.fmt.pix_mp.flags        != gfmt.fmt.pix_mp.flags)) {
		printf("Get format error. line=%d\n", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (alloc)                                           */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 1;		/* Request 1 buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_MMAP;

	ret = ioctl(src1_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QUERYBUF                                                  */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.index			= 0;
	buf.type			= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory			= V4L2_MEMORY_MMAP;
	buf.length			= VIDEO_MAX_PLANES;
	buf.m.planes			= planes;
	buf.m.planes[0].bytesused	= 0;

	ret = ioctl(src1_fd, VIDIOC_QUERYBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Mmap for source buffer                                           */
	/*-------------------------------------------------------------------*/
	psrc1_buf = mmap(0, SRC1_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
			src1_fd, 0);
	if (psrc1_buf == MAP_FAILED) {
		printf("Error(%d) : mmap", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Read file                                                        */
	/*-------------------------------------------------------------------*/
	ret = read_file(psrc1_buf, SRC1_SIZE, SRC1_FILENAME);
	if (ret == 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QBUF                                                      */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));

	buf.m.planes	= planes;
	buf.index	= 0;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.flags	= 0;
	buf.length	= 1;	/* Number of elements in the planes array. */
	buf.m.planes[0].bytesused	= SRC1_SIZE;
	buf.bytesused			= SRC1_SIZE;

	ret = ioctl(src1_fd, VIDIOC_QBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMON                                                  */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	ret = ioctl(src1_fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*********************************************************************
	 *  src2
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_S_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&fmt, 0, sizeof(fmt));
	fmt.type			= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.width		= SRC2_WIDTH;
	fmt.fmt.pix_mp.height		= SRC2_HEIGHT;
	fmt.fmt.pix_mp.field		= V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.pixelformat	= V4L2_PIX_FMT_ARGB32;
	fmt.fmt.pix_mp.num_planes	= 1;
	fmt.fmt.pix_mp.flags		= V4L2_PIX_FMT_FLAG_PREMUL_ALPHA;
	fmt.fmt.pix_mp.plane_fmt[0].bytesperline	= 0;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage		= 0;

	ret = ioctl(src2_fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_G_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&gfmt, 0x00, sizeof(gfmt));
	gfmt.type = fmt.type;
	ret = ioctl(src2_fd, VIDIOC_G_FMT, &gfmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	if ((fmt.fmt.pix_mp.width        != gfmt.fmt.pix_mp.width)       ||
	    (fmt.fmt.pix_mp.height       != gfmt.fmt.pix_mp.height)      ||
	    (fmt.fmt.pix_mp.field        != gfmt.fmt.pix_mp.field)       ||
	    (fmt.fmt.pix_mp.pixelformat  != gfmt.fmt.pix_mp.pixelformat) ||
	    (fmt.fmt.pix_mp.num_planes   != gfmt.fmt.pix_mp.num_planes)  ||
	    (fmt.fmt.pix_mp.flags        != gfmt.fmt.pix_mp.flags)) {
		printf("Get format error. line=%d\n", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (alloc)                                           */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 1;		/* Request 1 buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_MMAP;

	ret = ioctl(src2_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QUERYBUF                                                  */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.index			= 0;
	buf.type			= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory			= V4L2_MEMORY_MMAP;
	buf.length			= VIDEO_MAX_PLANES;
	buf.m.planes			= planes;
	buf.m.planes[0].bytesused	= 0;

	ret = ioctl(src2_fd, VIDIOC_QUERYBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Mmap for source buffer                                           */
	/*-------------------------------------------------------------------*/
	psrc2_buf = mmap(0, SRC2_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
			src2_fd, 0);
	if (psrc2_buf == MAP_FAILED) {
		printf("Error(%d) : mmap", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Make image                                                       */
	/*-------------------------------------------------------------------*/
	make_stripe_image((void *)psrc2_buf, SRC2_WIDTH, SRC2_HEIGHT);
	calc_img_premultiplied_alpha((void *)psrc2_buf, SRC2_WIDTH,
					SRC2_HEIGHT);

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QBUF                                                      */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));

	buf.m.planes	= planes;
	buf.index	= 0;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.flags	= 0;
	buf.length	= 1;	/* Number of elements in the planes array. */
	buf.m.planes[0].bytesused	= SRC2_SIZE;
	buf.bytesused			= SRC2_SIZE;

	ret = ioctl(src2_fd, VIDIOC_QBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMON                                                  */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	ret = ioctl(src2_fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*********************************************************************
	 *  dst
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_S_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&fmt, 0, sizeof(fmt));
	fmt.type			= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.width		= DST_WIDTH;
	fmt.fmt.pix_mp.height		= DST_HEIGHT;
	fmt.fmt.pix_mp.pixelformat	= V4L2_PIX_FMT_ARGB32;
	fmt.fmt.pix_mp.field		= V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.num_planes	= 1;		/* argb32 */
	fmt.fmt.pix_mp.plane_fmt[0].bytesperline	= 0;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage		= 0;

	ret = ioctl(dst_fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_G_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&gfmt, 0x00, sizeof(gfmt));
	gfmt.type = fmt.type;
	ret = ioctl(dst_fd, VIDIOC_G_FMT, &gfmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	if ((fmt.fmt.pix_mp.width        != gfmt.fmt.pix_mp.width)       ||
	    (fmt.fmt.pix_mp.height       != gfmt.fmt.pix_mp.height)      ||
	    (fmt.fmt.pix_mp.field        != gfmt.fmt.pix_mp.field)       ||
	    (fmt.fmt.pix_mp.pixelformat  != gfmt.fmt.pix_mp.pixelformat) ||
	    (fmt.fmt.pix_mp.num_planes   != gfmt.fmt.pix_mp.num_planes)  ||
	    (fmt.fmt.pix_mp.flags        != gfmt.fmt.pix_mp.flags)) {
		printf("Get format error. line=%d\n", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (alloc)                                           */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 1;		/* Request 1 buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req_buf.memory	= V4L2_MEMORY_MMAP;

	ret = ioctl(dst_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QUERYBUF                                                  */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.index			= 0;
	buf.type			= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory			= V4L2_MEMORY_MMAP;
	buf.length			= VIDEO_MAX_PLANES;
	buf.m.planes			= planes;
	buf.m.planes[0].bytesused	= 0;

	ret = ioctl(dst_fd, VIDIOC_QUERYBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Mmap for destination buffer                                      */
	/*-------------------------------------------------------------------*/
	pdst_buf = mmap(0, DST_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
			dst_fd, 0);
	if (pdst_buf == MAP_FAILED) {
		printf("Error(%d) : mmap", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QBUF                                                      */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));

	buf.m.planes	= planes;
	buf.index	= 0;
	buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.length	= 1;	/* Number of elements in the planes array. */
	buf.m.planes[0].bytesused	= DST_SIZE;

	ret = ioctl(dst_fd, VIDIOC_QBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMON                                                  */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(dst_fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_DQBUF                                                     */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.m.planes	= planes;
	buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.length	= VIDEO_MAX_PLANES;

	ret = ioctl(dst_fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Write file                                                       */
	/*-------------------------------------------------------------------*/
	ret = write_file(pdst_buf, DST_SIZE, DST_FILENAME_MMAP);
	if (ret == 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*********************************************************************
	 *  src1
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_DQBUF                                                     */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.m.planes	= planes;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.length	= 1;

	ret = ioctl(src1_fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMOFF                                                 */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ret = ioctl(src1_fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Unmap buffer                                                     */
	/*-------------------------------------------------------------------*/
	munmap(psrc1_buf, SRC1_SIZE);

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (release)                                         */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 0;		/* Release buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_MMAP;

	ret = ioctl(src1_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*********************************************************************
	 *  src2
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_DQBUF                                                     */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.m.planes	= planes;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_MMAP;
	buf.length	= 1;

	ret = ioctl(src2_fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMOFF                                                 */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ret = ioctl(src2_fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Unmap buffer                                                     */
	/*-------------------------------------------------------------------*/
	munmap(psrc2_buf, SRC2_SIZE);

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (release)                                         */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 0;		/* Release buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_MMAP;

	ret = ioctl(src2_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*********************************************************************
	 *  dst
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMOFF                                                 */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(dst_fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Unmap buffer                                                     */
	/*-------------------------------------------------------------------*/
	munmap(pdst_buf, DST_SIZE);

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (release)                                         */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 0;		/* Release buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req_buf.memory	= V4L2_MEMORY_MMAP;
	ret = ioctl(dst_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	close(src1_fd);
	close(src2_fd);
	close(dst_fd);

	media_device_unref(pmedia);

	return 0;
}

/******************************************************************************
 *  userptr
 ******************************************************************************/
static int test_bru_userptr(void)
{
	struct media_device  *pmedia;

	unsigned char  *psrc1_buf;
	unsigned char  *psrc2_buf;
	unsigned char  *pdst_buf;

	int src1_fd  = -1;	/* src file descriptor */
	int src2_fd  = -1;	/* src file descriptor */
	int dst_fd   = -1;	/* dst file descriptor */

	unsigned int  type;
	int           ret = -1;

	struct v4l2_format          fmt;
	struct v4l2_requestbuffers  req_buf;
	struct v4l2_buffer          buf;
	struct v4l2_capability      cap;
	struct v4l2_plane           planes[VIDEO_MAX_PLANES];

	unsigned int        caps;
	struct v4l2_format  gfmt;

	MMNGR_ID	src1fd;
	unsigned long	src1_phys;
	unsigned long	src1_hard;
	unsigned long	src1_virt;

	MMNGR_ID	src2fd;
	unsigned long	src2_phys;
	unsigned long	src2_hard;
	unsigned long	src2_virt;

	MMNGR_ID	dstfd;
	unsigned long	dst_phys;
	unsigned long	dst_hard;
	unsigned long	dst_virt;

	const char *pmedia_name;

	/*-------------------------------------------------------------------*/
	/*  Call media-ctl                                                   */
	/*-------------------------------------------------------------------*/
	ret = call_media_ctl(&pmedia, &pmedia_name);
	if (ret < 0) {
		printf("Error : media-ctl call failed.\n");
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Open device                                                      */
	/*-------------------------------------------------------------------*/
	/* src device(rpf.0) */
	src1_fd = open_video_device(pmedia, SRC1_INPUT_DEV, pmedia_name);
	if (src1_fd == -1) {
		printf("Error opening device: %s (%d).\n",
			strerror(errno), errno);
		return -1;
	}

	/* src device(rpf.1) */
	src2_fd = open_video_device(pmedia, SRC2_INPUT_DEV, pmedia_name);
	if (src2_fd == -1) {
		printf("Error open src2 device: %s (%d).\n",
			strerror(errno), errno);
		return -1;
	}

	/* dst device(wpf.0) */
	dst_fd = open_video_device(pmedia, DST_OUTPUT_DEV, pmedia_name);
	if (dst_fd == -1) {
		printf("Error opening device: %s (%d).\n",
			strerror(errno), errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QUERYCAP                                                  */
	/*-------------------------------------------------------------------*/
	/* src1 */
	memset(&cap, 0, sizeof(cap));
	ret = ioctl(src1_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}
	caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
	     ? cap.device_caps : cap.capabilities;

	if ((caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE) == 0) {
		printf("Device does not have required capabilitiy. line=%d\n",
			__LINE__);
		return -1;
	}

	/* src2 */
	memset(&cap, 0, sizeof(cap));
	ret = ioctl(src2_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}
	caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
	     ? cap.device_caps : cap.capabilities;

	if ((caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE) == 0) {
		printf("Device does not have required capabilitiy. line=%d\n",
			__LINE__);
		return -1;
	}

	/* dst */
	ret = ioctl(dst_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}
	caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
	     ? cap.device_caps : cap.capabilities;

	if ((caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0) {
		printf("Device does not have required capabilitiy. line=%d\n",
			__LINE__);
		return -1;
	}

	/*********************************************************************
	 *  src1
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_S_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&fmt, 0, sizeof(fmt));
	fmt.type			= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.width		= SRC1_WIDTH;
	fmt.fmt.pix_mp.height		= SRC1_HEIGHT;
	fmt.fmt.pix_mp.field		= V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.pixelformat	= V4L2_PIX_FMT_ARGB32;
	fmt.fmt.pix_mp.num_planes	= 1;
	fmt.fmt.pix_mp.plane_fmt[0].bytesperline	= 0;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage		= 0;

	ret = ioctl(src1_fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_G_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&gfmt, 0x00, sizeof(gfmt));
	gfmt.type = fmt.type;
	ret = ioctl(src1_fd, VIDIOC_G_FMT, &gfmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	if ((fmt.fmt.pix_mp.width        != gfmt.fmt.pix_mp.width)       ||
	    (fmt.fmt.pix_mp.height       != gfmt.fmt.pix_mp.height)      ||
	    (fmt.fmt.pix_mp.field        != gfmt.fmt.pix_mp.field)       ||
	    (fmt.fmt.pix_mp.pixelformat  != gfmt.fmt.pix_mp.pixelformat) ||
	    (fmt.fmt.pix_mp.num_planes   != gfmt.fmt.pix_mp.num_planes)  ||
	    (fmt.fmt.pix_mp.flags        != gfmt.fmt.pix_mp.flags)) {
		printf("Get format error. line=%d\n", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (alloc)                                           */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 1;		/* Request 1 buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_USERPTR;

	ret = ioctl(src1_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Allocate memory by mmngr                                         */
	/*-------------------------------------------------------------------*/
	ret = mmngr_alloc_in_user(
		&src1fd, SRC1_SIZE,
		&src1_phys, &src1_hard, &src1_virt, MMNGR_VA_SUPPORT);
	if (ret) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}
	psrc1_buf = (void *)src1_virt;

	/*-------------------------------------------------------------------*/
	/*  Read file                                                        */
	/*-------------------------------------------------------------------*/
	ret = read_file(psrc1_buf, SRC1_SIZE, SRC1_FILENAME);
	if (ret == 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QBUF                                                      */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));

	buf.m.planes	= planes;
	buf.index	= 0;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_USERPTR;
	buf.flags	= 0;
	buf.length	= 1;	/* Number of elements in the planes array. */
	buf.m.planes[0].bytesused	= SRC1_SIZE;
	buf.m.planes[0].length		= SRC1_SIZE;
	buf.m.planes[0].m.userptr	= (unsigned long)psrc1_buf;
	buf.bytesused			= SRC1_SIZE;

	ret = ioctl(src1_fd, VIDIOC_QBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMON                                                  */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	ret = ioctl(src1_fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*********************************************************************
	 *  src2
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_S_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&fmt, 0, sizeof(fmt));
	fmt.type			= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.width		= SRC2_WIDTH;
	fmt.fmt.pix_mp.height		= SRC2_HEIGHT;
	fmt.fmt.pix_mp.field		= V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.pixelformat	= V4L2_PIX_FMT_ARGB32;
	fmt.fmt.pix_mp.num_planes	= 1;
	fmt.fmt.pix_mp.flags		= V4L2_PIX_FMT_FLAG_PREMUL_ALPHA;
	fmt.fmt.pix_mp.plane_fmt[0].bytesperline	= 0;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage		= 0;

	ret = ioctl(src2_fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_G_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&gfmt, 0x00, sizeof(gfmt));
	gfmt.type = fmt.type;
	ret = ioctl(src2_fd, VIDIOC_G_FMT, &gfmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	if ((fmt.fmt.pix_mp.width        != gfmt.fmt.pix_mp.width)       ||
	    (fmt.fmt.pix_mp.height       != gfmt.fmt.pix_mp.height)      ||
	    (fmt.fmt.pix_mp.field        != gfmt.fmt.pix_mp.field)       ||
	    (fmt.fmt.pix_mp.pixelformat  != gfmt.fmt.pix_mp.pixelformat) ||
	    (fmt.fmt.pix_mp.num_planes   != gfmt.fmt.pix_mp.num_planes)  ||
	    (fmt.fmt.pix_mp.flags        != gfmt.fmt.pix_mp.flags)) {
		printf("Get format error. line=%d\n", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (alloc)                                           */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 1;		/* Request 1 buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_USERPTR;

	ret = ioctl(src2_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Allocate memory by mmngr                                         */
	/*-------------------------------------------------------------------*/
	ret = mmngr_alloc_in_user(
		&src2fd, SRC2_SIZE,
		&src2_phys, &src2_hard, &src2_virt, MMNGR_VA_SUPPORT);
	if (ret) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}
	psrc2_buf = (void *)src2_virt;

	/*-------------------------------------------------------------------*/
	/*  Make image                                                       */
	/*-------------------------------------------------------------------*/
	make_stripe_image((void *)psrc2_buf, SRC2_WIDTH, SRC2_HEIGHT);
	calc_img_premultiplied_alpha((void *)psrc2_buf, SRC2_WIDTH,
		SRC2_HEIGHT);

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QBUF                                                      */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));

	buf.m.planes	= planes;
	buf.index	= 0;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_USERPTR;
	buf.flags	= 0;
	buf.length	= 1;	/* Number of elements in the planes array. */
	buf.m.planes[0].bytesused	= SRC2_SIZE;
	buf.m.planes[0].length		= SRC2_SIZE;
	buf.m.planes[0].m.userptr	= (unsigned long)psrc2_buf;
	buf.bytesused			= SRC2_SIZE;

	ret = ioctl(src2_fd, VIDIOC_QBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMON                                                  */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	ret = ioctl(src2_fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*********************************************************************
	 *  dst
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_S_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&fmt, 0, sizeof(fmt));
	fmt.type			= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.width		= DST_WIDTH;
	fmt.fmt.pix_mp.height		= DST_HEIGHT;
	fmt.fmt.pix_mp.pixelformat	= V4L2_PIX_FMT_ARGB32;
	fmt.fmt.pix_mp.field		= V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.num_planes	= 1;		/* argb32 */
	fmt.fmt.pix_mp.plane_fmt[0].bytesperline	= 0;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage		= 0;

	ret = ioctl(dst_fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_G_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&gfmt, 0x00, sizeof(gfmt));
	gfmt.type = fmt.type;
	ret = ioctl(dst_fd, VIDIOC_G_FMT, &gfmt);
	if (ret < 0) {
		printf("error (%d) line=%d\n", ret, __LINE__);
		return -1;
	}

	if ((fmt.fmt.pix_mp.width        != gfmt.fmt.pix_mp.width)       ||
	    (fmt.fmt.pix_mp.height       != gfmt.fmt.pix_mp.height)      ||
	    (fmt.fmt.pix_mp.field        != gfmt.fmt.pix_mp.field)       ||
	    (fmt.fmt.pix_mp.pixelformat  != gfmt.fmt.pix_mp.pixelformat) ||
	    (fmt.fmt.pix_mp.num_planes   != gfmt.fmt.pix_mp.num_planes)  ||
	    (fmt.fmt.pix_mp.flags        != gfmt.fmt.pix_mp.flags)) {
		printf("Get format error. line=%d\n", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (alloc)                                           */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 1;		/* Request 1 buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req_buf.memory	= V4L2_MEMORY_USERPTR;

	ret = ioctl(dst_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Allocate memory by mmngr                                         */
	/*-------------------------------------------------------------------*/
	ret = mmngr_alloc_in_user(
		&dstfd, DST_SIZE,
		&dst_phys, &dst_hard, &dst_virt, MMNGR_VA_SUPPORT);
	if (ret) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}
	pdst_buf = (void *)dst_virt;

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QBUF                                                      */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));

	buf.m.planes	= planes;
	buf.index	= 0;
	buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory	= V4L2_MEMORY_USERPTR;
	buf.length	= 1;	/* Number of elements in the planes array. */
	buf.m.planes[0].bytesused	= DST_SIZE;
	buf.m.planes[0].length		= DST_SIZE;
	buf.m.planes[0].m.userptr	= (unsigned long)pdst_buf;

	ret = ioctl(dst_fd, VIDIOC_QBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMON                                                  */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(dst_fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_DQBUF                                                     */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.m.planes	= planes;
	buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory	= V4L2_MEMORY_USERPTR;
	buf.length	= VIDEO_MAX_PLANES;

	ret = ioctl(dst_fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Write file                                                       */
	/*-------------------------------------------------------------------*/
	ret = write_file(pdst_buf, DST_SIZE, DST_FILENAME_USERPTR);
	if (ret == 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*********************************************************************
	 *  src1
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_DQBUF                                                     */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.m.planes	= planes;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_USERPTR;
	buf.length	= 1;

	ret = ioctl(src1_fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMOFF                                                 */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ret = ioctl(src1_fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (release)                                         */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 0;		/* Release buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_USERPTR;

	ret = ioctl(src1_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Free buffer                                                      */
	/*-------------------------------------------------------------------*/
	ret = mmngr_free_in_user(src1fd);
	if (ret < 0) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	/*********************************************************************
	 *  src2
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_DQBUF                                                     */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.m.planes	= planes;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_USERPTR;
	buf.length	= 1;

	ret = ioctl(src2_fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMOFF                                                 */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ret = ioctl(src2_fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (release)                                         */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 0;		/* Release buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_USERPTR;

	ret = ioctl(src2_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Free buffer                                                      */
	/*-------------------------------------------------------------------*/
	ret = mmngr_free_in_user(src2fd);
	if (ret < 0) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	/*********************************************************************
	 *  dst
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMOFF                                                 */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(dst_fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (release)                                         */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 0;		/* Release buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req_buf.memory	= V4L2_MEMORY_USERPTR;
	ret = ioctl(dst_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Free buffer                                                      */
	/*-------------------------------------------------------------------*/
	ret = mmngr_free_in_user(dstfd);
	if (ret < 0) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	close(src1_fd);
	close(src2_fd);
	close(dst_fd);

	media_device_unref(pmedia);

	return 0;
}

/******************************************************************************
 *  dmabuf
 ******************************************************************************/
static int test_bru_dmabuf(void)
{
	struct media_device  *pmedia;

	unsigned char  *psrc1_buf;
	unsigned char  *psrc2_buf;
	unsigned char  *pdst_buf;

	int src1_fd  = -1;	/* src file descriptor */
	int src2_fd  = -1;	/* src file descriptor */
	int dst_fd   = -1;	/* dst file descriptor */

	unsigned int  type;
	int           ret = -1;

	struct v4l2_format          fmt;
	struct v4l2_requestbuffers  req_buf;
	struct v4l2_buffer          buf;
	struct v4l2_capability      cap;
	struct v4l2_plane           planes[VIDEO_MAX_PLANES];

	unsigned int        caps;
	struct v4l2_format  gfmt;

	MMNGR_ID	src1fd;
	unsigned long	src1_phys;
	unsigned long	src1_hard;
	unsigned long	src1_virt;
	int		src1_mbid;
	int		src1_dmafd;

	MMNGR_ID	src2fd;
	unsigned long	src2_phys;
	unsigned long	src2_hard;
	unsigned long	src2_virt;
	int		src2_mbid;
	int		src2_dmafd;

	MMNGR_ID	dstfd;
	unsigned long	dst_phys;
	unsigned long	dst_hard;
	unsigned long	dst_virt;
	int		dst_mbid;
	int		dst_dmafd;

	const char *pmedia_name;

	/*-------------------------------------------------------------------*/
	/*  Call media-ctl                                                   */
	/*-------------------------------------------------------------------*/
	ret = call_media_ctl(&pmedia, &pmedia_name);
	if (ret < 0) {
		printf("Error : media-ctl call failed.\n");
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Open device                                                      */
	/*-------------------------------------------------------------------*/
	/* src device(rpf.0) */
	src1_fd = open_video_device(pmedia, SRC1_INPUT_DEV, pmedia_name);
	if (src1_fd == -1) {
		printf("Error opening device: %s (%d).\n",
			strerror(errno), errno);
		return -1;
	}

	/* src device(rpf.1) */
	src2_fd = open_video_device(pmedia, SRC2_INPUT_DEV, pmedia_name);
	if (src2_fd == -1) {
		printf("Error open src2 device: %s (%d).\n",
			strerror(errno), errno);
		return -1;
	}

	/* dst device(wpf.0) */
	dst_fd = open_video_device(pmedia, DST_OUTPUT_DEV, pmedia_name);
	if (dst_fd == -1) {
		printf("Error opening device: %s (%d).\n",
			strerror(errno), errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QUERYCAP                                                  */
	/*-------------------------------------------------------------------*/
	/* src1 */
	memset(&cap, 0, sizeof(cap));
	ret = ioctl(src1_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}
	caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
	     ? cap.device_caps : cap.capabilities;

	if ((caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE) == 0) {
		printf("Device does not have required capabilitiy. line=%d\n",
			__LINE__);
		return -1;
	}

	/* src2 */
	memset(&cap, 0, sizeof(cap));
	ret = ioctl(src2_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}
	caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
	     ? cap.device_caps : cap.capabilities;

	if ((caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE) == 0) {
		printf("Device does not have required capabilitiy. line=%d\n",
			__LINE__);
		return -1;
	}

	/* dst */
	ret = ioctl(dst_fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}
	caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
	     ? cap.device_caps : cap.capabilities;

	if ((caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0) {
		printf("Device does not have required capabilitiy. line=%d\n",
			__LINE__);
		return -1;
	}

	/*********************************************************************
	 *  src1
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_S_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&fmt, 0, sizeof(fmt));
	fmt.type			= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.width		= SRC1_WIDTH;
	fmt.fmt.pix_mp.height		= SRC1_HEIGHT;
	fmt.fmt.pix_mp.field		= V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.pixelformat	= V4L2_PIX_FMT_ARGB32;
	fmt.fmt.pix_mp.num_planes	= 1;
	fmt.fmt.pix_mp.plane_fmt[0].bytesperline	= 0;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage		= 0;

	ret = ioctl(src1_fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_G_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&gfmt, 0x00, sizeof(gfmt));
	gfmt.type = fmt.type;
	ret = ioctl(src1_fd, VIDIOC_G_FMT, &gfmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	if ((fmt.fmt.pix_mp.width        != gfmt.fmt.pix_mp.width)       ||
	    (fmt.fmt.pix_mp.height       != gfmt.fmt.pix_mp.height)      ||
	    (fmt.fmt.pix_mp.field        != gfmt.fmt.pix_mp.field)       ||
	    (fmt.fmt.pix_mp.pixelformat  != gfmt.fmt.pix_mp.pixelformat) ||
	    (fmt.fmt.pix_mp.num_planes   != gfmt.fmt.pix_mp.num_planes)  ||
	    (fmt.fmt.pix_mp.flags        != gfmt.fmt.pix_mp.flags)) {
		printf("Get format error. line=%d\n", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (alloc)                                           */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 1;		/* Request 1 buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_DMABUF;

	ret = ioctl(src1_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Allocate memory by mmngr                                         */
	/*-------------------------------------------------------------------*/
	ret = mmngr_alloc_in_user(
		&src1fd, SRC1_SIZE,
		&src1_phys, &src1_hard, &src1_virt, MMNGR_VA_SUPPORT);
	if (ret) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}
	psrc1_buf = (void *)src1_virt;

	/*-------------------------------------------------------------------*/
	/*  Get dma buffer file descriptor by mmngr                          */
	/*-------------------------------------------------------------------*/
	ret = mmngr_export_start_in_user(&src1_mbid, SRC1_SIZE,
		src1_hard, &src1_dmafd);
	if (ret) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Read file                                                        */
	/*-------------------------------------------------------------------*/
	ret = read_file(psrc1_buf, SRC1_SIZE, SRC1_FILENAME);
	if (ret == 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QBUF                                                      */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));

	buf.m.planes	= planes;
	buf.index	= 0;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_DMABUF;
	buf.flags	= 0;
	buf.length	= 1;	/* Number of elements in the planes array. */
	buf.m.planes[0].m.fd		= src1_dmafd;
	buf.m.planes[0].bytesused	= SRC1_SIZE;
	buf.m.planes[0].length		= SRC1_SIZE;
	buf.bytesused			= SRC1_SIZE;

	ret = ioctl(src1_fd, VIDIOC_QBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMON                                                  */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	ret = ioctl(src1_fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*********************************************************************
	 *  src2
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_S_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&fmt, 0, sizeof(fmt));
	fmt.type			= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.width		= SRC2_WIDTH;
	fmt.fmt.pix_mp.height		= SRC2_HEIGHT;
	fmt.fmt.pix_mp.field		= V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.pixelformat	= V4L2_PIX_FMT_ARGB32;
	fmt.fmt.pix_mp.num_planes	= 1;
	fmt.fmt.pix_mp.flags		= V4L2_PIX_FMT_FLAG_PREMUL_ALPHA;
	fmt.fmt.pix_mp.plane_fmt[0].bytesperline	= 0;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage		= 0;

	ret = ioctl(src2_fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_G_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&gfmt, 0x00, sizeof(gfmt));
	gfmt.type = fmt.type;
	ret = ioctl(src2_fd, VIDIOC_G_FMT, &gfmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	if ((fmt.fmt.pix_mp.width        != gfmt.fmt.pix_mp.width)       ||
	    (fmt.fmt.pix_mp.height       != gfmt.fmt.pix_mp.height)      ||
	    (fmt.fmt.pix_mp.field        != gfmt.fmt.pix_mp.field)       ||
	    (fmt.fmt.pix_mp.pixelformat  != gfmt.fmt.pix_mp.pixelformat) ||
	    (fmt.fmt.pix_mp.num_planes   != gfmt.fmt.pix_mp.num_planes)  ||
	    (fmt.fmt.pix_mp.flags        != gfmt.fmt.pix_mp.flags)) {
		printf("Get format error. line=%d\n", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (alloc)                                           */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 1;		/* Request 1 buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_DMABUF;

	ret = ioctl(src2_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Allocate memory by mmngr                                         */
	/*-------------------------------------------------------------------*/
	ret = mmngr_alloc_in_user(
		&src2fd, SRC2_SIZE,
		&src2_phys, &src2_hard, &src2_virt, MMNGR_VA_SUPPORT);
	if (ret) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}
	psrc2_buf = (void *)src2_virt;

	/*-------------------------------------------------------------------*/
	/*  Get dma buffer file descriptor by mmngr                          */
	/*-------------------------------------------------------------------*/
	ret = mmngr_export_start_in_user(&src2_mbid, SRC2_SIZE,
		src2_hard, &src2_dmafd);
	if (ret) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Make image                                                       */
	/*-------------------------------------------------------------------*/
	make_stripe_image((void *)psrc2_buf, SRC2_WIDTH, SRC2_HEIGHT);
	calc_img_premultiplied_alpha((void *)psrc2_buf, SRC2_WIDTH,
		SRC2_HEIGHT);

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QBUF                                                      */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));

	buf.m.planes	= planes;
	buf.index	= 0;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_DMABUF;
	buf.flags	= 0;
	buf.length	= 1;	/* Number of elements in the planes array. */
	buf.m.planes[0].m.fd		= src2_dmafd;
	buf.m.planes[0].bytesused	= SRC2_SIZE;
	buf.m.planes[0].length		= SRC2_SIZE;
	buf.bytesused			= SRC2_SIZE;

	ret = ioctl(src2_fd, VIDIOC_QBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMON                                                  */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	ret = ioctl(src2_fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*********************************************************************
	 *  dst
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_S_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&fmt, 0, sizeof(fmt));
	fmt.type			= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.width		= DST_WIDTH;
	fmt.fmt.pix_mp.height		= DST_HEIGHT;
	fmt.fmt.pix_mp.pixelformat	= V4L2_PIX_FMT_ARGB32;
	fmt.fmt.pix_mp.field		= V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.num_planes	= 1;		/* argb32 */
	fmt.fmt.pix_mp.plane_fmt[0].bytesperline	= 0;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage		= 0;

	ret = ioctl(dst_fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_G_FMT                                                     */
	/*-------------------------------------------------------------------*/
	memset(&gfmt, 0x00, sizeof(gfmt));
	gfmt.type = fmt.type;
	ret = ioctl(dst_fd, VIDIOC_G_FMT, &gfmt);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	if ((fmt.fmt.pix_mp.width        != gfmt.fmt.pix_mp.width)       ||
	    (fmt.fmt.pix_mp.height       != gfmt.fmt.pix_mp.height)      ||
	    (fmt.fmt.pix_mp.field        != gfmt.fmt.pix_mp.field)       ||
	    (fmt.fmt.pix_mp.pixelformat  != gfmt.fmt.pix_mp.pixelformat) ||
	    (fmt.fmt.pix_mp.num_planes   != gfmt.fmt.pix_mp.num_planes)  ||
	    (fmt.fmt.pix_mp.flags        != gfmt.fmt.pix_mp.flags)) {
		printf("Get format error. line=%d\n", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (alloc)                                           */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 1;		/* Request 1 buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req_buf.memory	= V4L2_MEMORY_DMABUF;

	ret = ioctl(dst_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Allocate memory by mmngr                                         */
	/*-------------------------------------------------------------------*/
	ret = mmngr_alloc_in_user(
		&dstfd, DST_SIZE,
		&dst_phys, &dst_hard, &dst_virt, MMNGR_VA_SUPPORT);
	if (ret) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}
	pdst_buf = (void *)dst_virt;

	/*-------------------------------------------------------------------*/
	/*  Get dma buffer file descriptor by mmngr                          */
	/*-------------------------------------------------------------------*/
	ret = mmngr_export_start_in_user(&dst_mbid, DST_SIZE,
		dst_hard, &dst_dmafd);
	if (ret) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_QBUF                                                      */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));

	buf.m.planes	= planes;
	buf.index	= 0;
	buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory	= V4L2_MEMORY_DMABUF;
	buf.length	= 1;	/* Number of elements in the planes array. */
	buf.m.planes[0].m.fd		= dst_dmafd;
	buf.m.planes[0].bytesused	= DST_SIZE;
	buf.m.planes[0].length		= DST_SIZE;
	buf.bytesused			= DST_SIZE;

	ret = ioctl(dst_fd, VIDIOC_QBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMON                                                  */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(dst_fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_DQBUF                                                     */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.m.planes	= planes;
	buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory	= V4L2_MEMORY_DMABUF;
	buf.length	= VIDEO_MAX_PLANES;

	ret = ioctl(dst_fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Write file                                                       */
	/*-------------------------------------------------------------------*/
	ret = write_file(pdst_buf, DST_SIZE, DST_FILENAME_DMABUF);
	if (ret == 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*********************************************************************
	 *  src1
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_DQBUF                                                     */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.m.planes	= planes;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_DMABUF;
	buf.length	= 1;

	ret = ioctl(src1_fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMOFF                                                 */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ret = ioctl(src1_fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Release dma buffer file descriptor by mmngr                      */
	/*-------------------------------------------------------------------*/
	ret = mmngr_export_end_in_user(src1_mbid);
	if (ret < 0) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (release)                                         */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 0;		/* Release buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_DMABUF;

	ret = ioctl(src1_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error %d\n", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Free buffer                                                      */
	/*-------------------------------------------------------------------*/
	ret = mmngr_free_in_user(src1fd);
	if (ret < 0) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	/*********************************************************************
	 *  src2
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_DQBUF                                                     */
	/*-------------------------------------------------------------------*/
	memset(&buf, 0, sizeof(buf));
	memset(planes, 0, sizeof(planes));
	buf.m.planes	= planes;
	buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory	= V4L2_MEMORY_DMABUF;
	buf.length	= 1;

	ret = ioctl(src2_fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMOFF                                                 */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ret = ioctl(src2_fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Release dma buffer file descriptor by mmngr                      */
	/*-------------------------------------------------------------------*/
	ret = mmngr_export_end_in_user(src2_mbid);
	if (ret < 0) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (release)                                         */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 0;		/* Release buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_buf.memory	= V4L2_MEMORY_DMABUF;

	ret = ioctl(src2_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error %d\n", __LINE__);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Free buffer                                                      */
	/*-------------------------------------------------------------------*/
	ret = mmngr_free_in_user(src2fd);
	if (ret < 0) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	/*********************************************************************
	 *  dst
	 *********************************************************************/
	/*-------------------------------------------------------------------*/
	/*  VIDIOC_STREAMOFF                                                 */
	/*-------------------------------------------------------------------*/
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(dst_fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Release dma buffer file descriptor by mmngr                      */
	/*-------------------------------------------------------------------*/
	ret = mmngr_export_end_in_user(dst_mbid);
	if (ret < 0) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  VIDIOC_REQBUFS (release)                                         */
	/*-------------------------------------------------------------------*/
	memset(&req_buf, 0, sizeof(req_buf));
	req_buf.count	= 0;		/* Release buffers */
	req_buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req_buf.memory	= V4L2_MEMORY_DMABUF;
	ret = ioctl(dst_fd, VIDIOC_REQBUFS, &req_buf);
	if (ret < 0) {
		printf("error line=%d errno=(%d)\n", __LINE__, errno);
		return -1;
	}

	/*-------------------------------------------------------------------*/
	/*  Free buffer                                                      */
	/*-------------------------------------------------------------------*/
	ret = mmngr_free_in_user(dstfd);
	if (ret < 0) {
		printf("error line=%d errcode=(%d)\n", __LINE__, ret);
		return -1;
	}

	close(src1_fd);
	close(src2_fd);
	close(dst_fd);

	media_device_unref(pmedia);

	return 0;
}

/******************************************************************************
 *  internal function
 ******************************************************************************/
static int read_file(
	unsigned char	*pbuffers,
	unsigned int	size,
	const char	*pfilename
	)
{
	FILE	*fp;
	int	ret;

	fp = fopen(pfilename, "rb");
	if (fp == NULL) {
		printf("file open error...\n");
		ret = 0;
	} else {
		ret = fread(pbuffers, size, 1, fp);
		if (ret == 0)
			printf("buffer read error...\n");
		fclose(fp);
	}
	return ret;
}

static int write_file(
	unsigned char	*pbuffers,
	unsigned int	size,
	const char	*pfilename
	)
{
	FILE	*fp;
	int	ret;

	/* file output */
	fp = fopen(pfilename, "wb");
	if (!fp) {
		printf("dst file open error..\n");
		ret = 0;
	} else {
		ret = fwrite(pbuffers, size, 1, fp);
		if (ret == 0)
			printf("buffer write error...\n");
		fclose(fp);
	}
	return ret;
}

static int call_media_ctl(struct media_device **ppmedia,
			  const char **ppmedia_name)
{
	struct media_device		*pmedia;
	struct media_link		*plink;
	char				*endp;
	struct dev_pads			rpf0pad;
	struct dev_pads			rpf1pad;
	struct dev_brupads		brupad;
	struct dev_pads			wpfpad;
	struct v4l2_mbus_framefmt	format;
	struct v4l2_rect		rect;

	const struct media_device_info	*pinfo;
	char		*p;
	const char	*pname;
	char		buf[128];

	/* Initialize v4l2 media controller */
	pmedia = media_device_new(MEDIA_DEV_NAME);
	if (!pmedia) {
		printf("Error : media_device_new()\n");
		return -1;
	}

	*ppmedia = pmedia;

	if (media_device_enumerate(pmedia) != 0) {
		printf("Error : media_device_enumerate()\n");
		return -1;
	}

	if (media_reset_links(pmedia) != 0) {
		printf("Error : media_reset_links()\n");
		return -1;
	}

	/* get media device name */
	pinfo = media_get_info(pmedia);
	p = strchr(pinfo->bus_info, ':');
	if (p)
		pname = p + 1;
	else
		pname = pinfo->bus_info;

	*ppmedia_name = pname;

	/*----------------------*/
	/* rpf.0:1 -> bru:0     */
	/*----------------------*/
	sprintf(buf, "'%s rpf.0':1 -> '%s bru':0", pname, pname);
	plink = media_parse_link(pmedia, buf, &endp);
	if (plink == NULL) {
		printf("Error : media_parse_link(rpf.0 -> bru)\n");
		return -1;
	}
	/*------------------------*/
	if (media_setup_link(pmedia, plink->source, plink->sink, 1) != 0) {
		printf("Error : media_setup_link(rpf.0 -> bru)\n");
		return -1;
	}
	/*----------------------*/
	/* rpf.1:1 -> bru:1     */
	/*----------------------*/
	sprintf(buf, "'%s rpf.1':1 -> '%s bru':1", pname, pname);
	plink = media_parse_link(pmedia, buf, &endp);
	if (plink == NULL) {
		printf("Error : media_parse_link(rpf.1 -> bru)\n");
		return -1;
	}
	/*------------------------*/
	if (media_setup_link(pmedia, plink->source, plink->sink, 1) != 0) {
		printf("Error : media_setup_link(rpf.1 -> bru)\n");
		return -1;
	}
	/*----------------------*/
	/* bru:5 -> wpf.0:0     */
	/*----------------------*/
	sprintf(buf, "'%s bru':5 -> '%s wpf.0':0", pname, pname);
	plink = media_parse_link(pmedia, buf, &endp);
	if (plink == NULL) {
		printf("Error : media_parse_link(bru -> wpf)\n");
		return -1;
	}
	/*------------------------*/
	if (media_setup_link(pmedia, plink->source, plink->sink, 1) != 0) {
		printf("Error : media_setup_link(bru -> wpf)\n");
		return -1;
	}
	/*----------------------*/
	/* wpf.0:1 -> output    */
	/*----------------------*/
	sprintf(buf, "'%s wpf.0':1 -> '%s wpf.0 output':0", pname, pname);
	plink = media_parse_link(pmedia, buf, &endp);
	if (plink == NULL) {
		printf("Error : media_parse_link(wpf -> output)\n");
		return -1;
	}
	/*------------------------*/
	if (media_setup_link(pmedia, plink->source, plink->sink, 1) != 0) {
		printf("Error : media_setup_link(wpf -> output)\n");
		return -1;
	}

	/*---------------------------------------------------- get pads */
	/*----------------------*/
	/* rpf0 pad             */
	/*----------------------*/
	sprintf(buf, "'%s rpf.0':0", pname);
	rpf0pad.ppad0 = media_parse_pad(pmedia, buf, NULL);
	if (rpf0pad.ppad0 == NULL) {
		printf("Error : media_parse_pad(rpf.0 pad 0)\n");
		return -1;
	}
	/*------------------------*/
	sprintf(buf, "'%s rpf.0':1", pname);
	rpf0pad.ppad1 = media_parse_pad(pmedia, buf, NULL);
	if (rpf0pad.ppad1 == NULL) {
		printf("Error : media_parse_pad(rpf.0 pad 1)\n");
		return -1;
	}
	/*----------------------*/
	/* rpf1 pad             */
	/*----------------------*/
	sprintf(buf, "'%s rpf.1':0", pname);
	rpf1pad.ppad0 = media_parse_pad(pmedia, buf, NULL);
	if (rpf1pad.ppad0 == NULL) {
		printf("Error : media_parse_pad(rpf.1 pad 0)\n");
		return -1;
	}
	/*------------------------*/
	sprintf(buf, "'%s rpf.1':1", pname);
	rpf1pad.ppad1 = media_parse_pad(pmedia, buf, NULL);
	if (rpf1pad.ppad1 == NULL) {
		printf("Error : media_parse_pad(rpf.1 pad 1)\n");
		return -1;
	}
	/*----------------------*/
	/* bru pad              */
	/*----------------------*/
	sprintf(buf, "'%s bru':0", pname);
	brupad.ppad0 = media_parse_pad(pmedia, buf, NULL);
	if (brupad.ppad0 == NULL) {
		printf("Error : media_parse_pad(bru pad 0)\n");
		return -1;
	}
	/*------------------------*/
	sprintf(buf, "'%s bru':1", pname);
	brupad.ppad1 = media_parse_pad(pmedia, buf, NULL);
	if (brupad.ppad1 == NULL) {
		printf("Error : media_parse_pad(bru pad 1)\n");
		return -1;
	}
	/*------------------------*/
	sprintf(buf, "'%s bru':5", pname);
	brupad.ppad5 = media_parse_pad(pmedia, buf, NULL);
	if (brupad.ppad5 == NULL) {
		printf("Error : media_parse_pad(bru pad 5)\n");
		return -1;
	}
	/*----------------------*/
	/* wpf pad              */
	/*----------------------*/
	sprintf(buf, "'%s wpf.0':0", pname);
	wpfpad.ppad0 = media_parse_pad(pmedia, buf, NULL);
	if (wpfpad.ppad0 == NULL) {
		printf("Error : media_parse_pad(wpf pad 0)\n");
		return -1;
	}
	/*------------------------*/
	sprintf(buf, "'%s wpf.0':1", pname);
	wpfpad.ppad1 = media_parse_pad(pmedia, buf, NULL);
	if (wpfpad.ppad1 == NULL) {
		printf("Error : media_parse_pad(wpf pad 1)\n");
		return -1;
	}

	/*------------------------------------------------------- set format */

	/*----------------------*/
	/* rpf.0:0              */
	/*----------------------*/
	format.width	= SRC1_WIDTH;
	format.height	= SRC1_HEIGHT;
	format.code		= V4L2_MBUS_FMT_ARGB8888_1X32;
	if (v4l2_subdev_set_format(rpf0pad.ppad0->entity, &format,
		rpf0pad.ppad0->index, V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_format(rpf.0 pad 0)\n");
		return -1;
	}
	/*----------------------*/
	/* rpf.0:1              */
	/*----------------------*/
	if (v4l2_subdev_set_format(rpf0pad.ppad1->entity, &format,
		rpf0pad.ppad1->index, V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_format(rpf.0 pad 1)\n");
		return -1;
	}
	/*----------------------*/
	/* rpf.1:0              */
	/*----------------------*/
	format.width	= SRC2_WIDTH;
	format.height	= SRC2_HEIGHT;
	format.code		= V4L2_MBUS_FMT_ARGB8888_1X32;
	if (v4l2_subdev_set_format(rpf1pad.ppad0->entity, &format,
		rpf1pad.ppad0->index, V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_format(rpf.1 pad 0)\n");
		return -1;
	}
	/*----------------------*/
	/* rpf.1:1              */
	/*----------------------*/
	if (v4l2_subdev_set_format(rpf1pad.ppad1->entity, &format,
		rpf1pad.ppad1->index, V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_format(rpf.1 pad 1)\n");
		return -1;
	}
	/*----------------------*/
	/* bru:0                */
	/*----------------------*/
	format.width	= SRC1_WIDTH;
	format.height	= SRC1_HEIGHT;
	format.code		= V4L2_MBUS_FMT_ARGB8888_1X32;
	if (v4l2_subdev_set_format(brupad.ppad0->entity, &format,
		brupad.ppad0->index, V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_format(bru pad 0)\n");
		return -1;
	}
	/*----------------------*/
	/* bru:1                */
	/*----------------------*/
	format.width	= SRC2_WIDTH;
	format.height	= SRC2_HEIGHT;
	format.code		= V4L2_MBUS_FMT_ARGB8888_1X32;
	if (v4l2_subdev_set_format(brupad.ppad1->entity, &format,
		brupad.ppad1->index, V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_format(bru pad 1)\n");
		return -1;
	}
	/*----------------------*/
	/* bru:5                */
	/*----------------------*/
	format.width	= DST_WIDTH;
	format.height	= DST_HEIGHT;
	format.code		= V4L2_MBUS_FMT_ARGB8888_1X32;
	if (v4l2_subdev_set_format(brupad.ppad5->entity, &format,
		brupad.ppad5->index, V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_format(bru pad 5)\n");
		return -1;
	}
	/*----------------------*/
	/* wpf.0:0              */
	/*----------------------*/
	if (v4l2_subdev_set_format(wpfpad.ppad0->entity, &format,
		wpfpad.ppad0->index, V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_format(wpf pad 0)\n");
		return -1;
	}
	/*----------------------*/
	/* wpf.0:1              */
	/*----------------------*/
	if (v4l2_subdev_set_format(wpfpad.ppad1->entity, &format,
		wpfpad.ppad1->index, V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_format(wpf pad 1)\n");
		return -1;
	}
	/*----------------------*/
	/* rpf.0:0 crop         */
	/*----------------------*/
	rect.left   = 0;
	rect.top    = 0;
	rect.width  = SRC1_WIDTH;
	rect.height = SRC1_HEIGHT;
	if (v4l2_subdev_set_selection(rpf0pad.ppad0->entity, &rect,
		rpf0pad.ppad0->index, V4L2_SEL_TGT_CROP,
		V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_selection(rpf.0 pad 0)\n");
		return -1;
	}
	/*----------------------*/
	/* rpf.1:0 crop         */
	/*----------------------*/
	rect.left   = 0;
	rect.top    = 0;
	rect.width  = SRC2_WIDTH;
	rect.height = SRC2_HEIGHT;
	if (v4l2_subdev_set_selection(rpf1pad.ppad0->entity, &rect,
		rpf1pad.ppad0->index, V4L2_SEL_TGT_CROP,
		V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_selection(rpf.1 pad 0)\n");
		return -1;
	}
	/*----------------------*/
	/* bru:0 compose        */
	/*----------------------*/
	rect.left   = 0;
	rect.top    = 0;
	rect.width  = SRC1_WIDTH;
	rect.height = SRC1_HEIGHT;
	if (v4l2_subdev_set_selection(brupad.ppad0->entity, &rect,
		brupad.ppad0->index, V4L2_SEL_TGT_COMPOSE,
		V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_selection(bru pad 0)\n");
		return -1;
	}
	/*----------------------*/
	/* bru:1 compose        */
	/*----------------------*/
	rect.left   = 50;
	rect.top    = 50;
	rect.width  = SRC2_WIDTH;
	rect.height = SRC2_HEIGHT;
	if (v4l2_subdev_set_selection(brupad.ppad1->entity, &rect,
		brupad.ppad1->index, V4L2_SEL_TGT_COMPOSE,
		V4L2_SUBDEV_FORMAT_ACTIVE) != 0) {
		printf("Error : v4l2_subdev_set_selection(bru pad 1)\n");
		return -1;
	}
	return 0;
}

static void make_stripe_image(void *pbuf, int width, int height)
{
	unsigned int *pwk = (unsigned int *)pbuf;

	/* calc 5 lines */
	int height_part		= height / 5;
	int height_last_part	= height_part +  height % 5;

	int count_part		= width * height_part;
	int count_last_part	= width * height_last_part;

	/* make 5 line stripe */
	/* alpha_val : 0xff green:0xff */
	make_color(pwk, 0x00ff00ff, count_part);
	pwk += count_part;
	/* alpha_val : 0x80 green:0xff */
	make_color(pwk, 0x00ff0080, count_part);
	pwk += count_part;
	/* alpha_val : 0x00 green:0xff */
	make_color(pwk, 0x00ff0000, count_part);
	pwk += count_part;
	/* alpha_val : 0x80 red  :0xff */
	make_color(pwk, 0x0000ff80, count_part);
	pwk += count_part;
	/* alpha_val : 0xff red  :0xff */
	make_color(pwk, 0x0000ffff, count_last_part);
}

static void make_color(unsigned int *ptr, unsigned int color, int count)
{
	int i;

	for (i = 0; i < count; i++)
		*ptr++ = color;

}

static void calc_img_premultiplied_alpha(void *pbuf, int width, int height)
{
	int x, y;

	unsigned int *pwkbuf = (unsigned int *)pbuf;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			unsigned int wk = *pwkbuf;

			/* Get argb element */
			unsigned int a = (wk >>  0)&(0x000000ff);
			unsigned int r = (wk >>  8)&(0x000000ff);
			unsigned int g = (wk >> 16)&(0x000000ff);
			unsigned int b = (wk >> 24)&(0x000000ff);

			/* Calc rgb * alpha value */
			r = r * a / 255;
			g = g * a / 255;
			b = b * a / 255;

			wk  = (a & 0x000000ff) << 0
			    | (r & 0x000000ff) << 8
			    | (g & 0x000000ff) << 16
			    | (b & 0x000000ff) << 24
			    ;
			*pwkbuf++ = wk;
		}
	}
}

static int open_video_device(struct media_device *pmedia, char *pentity_base,
			     const char *pmedia_name)
{
	char entity_name[32];
	const char *pdevname;
	struct media_entity *pentity;

	snprintf(entity_name, sizeof(entity_name), pentity_base, pmedia_name);
	pentity = media_get_entity_by_name(pmedia, entity_name,
					   strlen(entity_name));
	if (!pentity) {
		printf("Error media_get_entity(%s)\n", entity_name);
		return -1;
	}
	pdevname = media_entity_get_devname(pentity);

	return open(pdevname, O_RDWR);
}
