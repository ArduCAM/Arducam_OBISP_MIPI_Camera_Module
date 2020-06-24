/*
 * arducamstill V4L2 Application

 */

#define __STDC_FORMAT_MACROS
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <sched.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>

#include <linux/videodev2.h>

#include "interface/mmal/mmal.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/util/mmal_connection.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/mmal_component.h"
#include "bcm_host.h"
#include "user-vcsm.h"
#include "arducamCtl.h"

#ifndef V4L2_BUF_FLAG_ERROR
#define V4L2_BUF_FLAG_ERROR	0x0040
#endif

#define ARRAY_SIZE(a)	(sizeof(a)/sizeof((a)[0]))

int debug = 1;
#define print(...) do { if (debug) printf(__VA_ARGS__); }  while (0)
long runTime = 5000;
enum buffer_fill_mode
{
	BUFFER_FILL_NONE = 0,
	BUFFER_FILL_FRAME = 1 << 0,
	BUFFER_FILL_PADDING = 1 << 1,
};

struct buffer
{
	unsigned int idx;
	unsigned int padding[VIDEO_MAX_PLANES];
	unsigned int size[VIDEO_MAX_PLANES];
	void *mem[VIDEO_MAX_PLANES];
	MMAL_BUFFER_HEADER_T *mmal;
	int dma_fd;
	unsigned int vcsm_handle;
};

typedef struct {
    int quality;            /// JPEG quality setting (1-100)
    uint32_t encoding; /// Encoding to use for the output file.
} IMAGE_ENCODER_STATE;

typedef struct mmal_param_thumbnail_config_s
{
   int enable;
   int width,height;
   int quality;
} MMAL_PARAM_THUMBNAIL_CONFIG_T;

typedef struct {
    uint32_t encoding;         /// Requested codec video encoding (MJPEG or H264)
    int bitrate;               /// Requested bitrate
    int intraperiod;           /// Intra-refresh period (key frame rate)
    int quantisationParameter; /// Quantisation parameter - quality. Set bitrate 0 and set this for variable bitrate
    int bInlineHeaders;        /// Insert inline headers to stream (SPS, PPS)
    int immutableInput;        /// Flag to specify whether encoder works in place or creates a new buffer. Result is preview can display either
                               /// the camera output or the encoder output (with compression artifacts)
    int profile;               /// H264 profile to use for encoding
    int level;                 /// H264 level to use for encoding

    int inlineMotionVectors; /// Encoder outputs inline Motion Vectors
    int intra_refresh_type;  /// What intra refresh type to use. -1 to not set.
    int addSPSTiming;        /// 0 or 1
    int slices;              /// Horizontal slices per frame. Default 1 (off)
} VIDEO_ENCODER_STATE;
struct device
{
	int fd;
	int opened;

	enum v4l2_buf_type type;
	enum v4l2_memory memtype;
	unsigned int nbufs;
	struct buffer *buffers;

	MMAL_COMPONENT_T *isp;
	MMAL_COMPONENT_T *render;
	MMAL_COMPONENT_T *encoder;
	MMAL_POOL_T *isp_output_pool;
	MMAL_POOL_T *isp_output_pool2;
	MMAL_POOL_T *render_pool;
	MMAL_POOL_T *encode_pool;
	MMAL_POOL_T *encoder_input_pool;
	MMAL_POOL_T *encoder_output_pool;
	MMAL_BOOL_T can_zero_copy;


	/* V4L2 to MMAL interface */
	MMAL_QUEUE_T *isp_queue;
	MMAL_POOL_T *mmal_pool;
	/* Encoded data */
	MMAL_POOL_T *output_pool;



	unsigned int width;
	unsigned int height;
	unsigned int fps;
	unsigned int frame_time_usec;
	uint32_t buffer_output_flags;
	uint32_t timestamp_type;
	struct timeval starttime;
	int64_t lastpts;
  
	unsigned char num_planes;
	struct v4l2_plane_pix_format plane_fmt[VIDEO_MAX_PLANES];

	void *pattern[VIDEO_MAX_PLANES];
	unsigned int patternsize[VIDEO_MAX_PLANES];

	bool write_data_prefix;

	VCOS_THREAD_T save_thread;
	VCOS_THREAD_T runTime_thread;
	MMAL_QUEUE_T *save_queue;
	int runTime; 
	bool stopStreaming;
	bool saveImageFlag;
	int thread_quit;
	FILE *image_fd;
	char *imageName;
	IMAGE_ENCODER_STATE  encoderFmt;
	int restart_interval;               /// JPEG restart interval. 0 for none.
	MMAL_PARAM_THUMBNAIL_CONFIG_T thumbnailConfig;
	struct v4l2_format_info *info;
	unsigned int pixelformat;
	int opacity;		// Opacity of window - 0 = transparent, 255 = opaque
	int fullscreen;		// 0 is use previewRect, non-zero to use full screen
   	MMAL_RECT_T preview_window;	// Destination rectangle for the preview window.  
	uint32_t arducamCtlName; 
    int arducamCtlValue; 
	bool isp_output0Flag;
	bool isp_output1Flag;

};
unsigned int capabilities = V4L2_CAP_VIDEO_CAPTURE;
#define VIDEO_PROFILE_H264_BASELINE 0x19
#define VIDEO_PROFILE_H264_MAIN 0x1A
#define VIDEO_PROFILE_H264_HIGH 0x1C
#define VIDEO_LEVEL_H264_4 0x1C
#define VIDEO_LEVEL_H264_41 0x1D
#define VIDEO_LEVEL_H264_42 0x1E
static void default_video_status(VIDEO_ENCODER_STATE *state) {
    // Default everything to zero
    memset(state, 0, sizeof(VIDEO_ENCODER_STATE));
    state->encoding = MMAL_ENCODING_H264;
    state->bitrate = 17000000;
    state->immutableInput = 1; // Not working
    /**********************H264 only**************************************/
    state->intraperiod = -1;                  // Not set
                                              // Specify the intra refresh period (key frame rate/GoP size).
                                              // Zero to produce an initial I-frame and then just P-frames.
    state->quantisationParameter = 0;         // Quantisation parameter. Use approximately 10-40. Default 0 (off)
    state->profile = VIDEO_PROFILE_H264_HIGH; // Specify H264 profile to use for encoding
    state->level = VIDEO_LEVEL_H264_4;        // Specify H264 level to use for encoding
    state->bInlineHeaders = 0;                // Insert inline headers (SPS, PPS) to stream
    state->inlineMotionVectors = 0;           // output motion vector estimates
    state->intra_refresh_type = -1;           // Set intra refresh type
    state->addSPSTiming = 0;                  // zero or one
    state->slices = 1;
    /**********************H264 only**************************************/
}
static void errno_exit(const char *s)
{
        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
        exit(EXIT_FAILURE);
}

static bool video_is_mplane(struct device *dev)
{
	return dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ||
	       dev->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
}

#define MMAL_ENCODING_UNUSED 0

static struct v4l2_format_info {
	char *name;
	unsigned int fourcc;
	unsigned char n_planes;
	MMAL_FOURCC_T mmal_encoding;
} pixel_formats[] = {
	{ "RGB332", V4L2_PIX_FMT_RGB332, 1, 	MMAL_ENCODING_UNUSED },
	{ "RGB444", V4L2_PIX_FMT_RGB444, 1,	MMAL_ENCODING_UNUSED },
	{ "ARGB444", V4L2_PIX_FMT_ARGB444, 1,	MMAL_ENCODING_UNUSED },
	{ "XRGB444", V4L2_PIX_FMT_XRGB444, 1,	MMAL_ENCODING_UNUSED },
	{ "RGB555", V4L2_PIX_FMT_RGB555, 1,	MMAL_ENCODING_UNUSED },
	{ "ARGB555", V4L2_PIX_FMT_ARGB555, 1,	MMAL_ENCODING_UNUSED },
	{ "XRGB555", V4L2_PIX_FMT_XRGB555, 1,	MMAL_ENCODING_UNUSED },
	{ "RGB565", V4L2_PIX_FMT_RGB565, 1,		MMAL_ENCODING_RGB16 },
	{ "BGR888", V4L2_PIX_FMT_BGR24, 1,		MMAL_ENCODING_BGR24 },
	{ "RGB888", V4L2_PIX_FMT_RGB24, 1,		MMAL_ENCODING_RGB24 },
	{ "RGB555X", V4L2_PIX_FMT_RGB555X, 1,	MMAL_ENCODING_UNUSED },
	{ "RGB565X", V4L2_PIX_FMT_RGB565X, 1,	MMAL_ENCODING_RGB16 },
	{ "BGR666", V4L2_PIX_FMT_BGR666, 1,	MMAL_ENCODING_UNUSED },
	{ "BGR24", V4L2_PIX_FMT_BGR24, 1,	MMAL_ENCODING_RGB24 },
	{ "RGB24", V4L2_PIX_FMT_RGB24, 1,	MMAL_ENCODING_BGR24 },
	{ "BGR32", V4L2_PIX_FMT_BGR32, 1,	MMAL_ENCODING_BGR32 },
	{ "ABGR32", V4L2_PIX_FMT_ABGR32, 1,	MMAL_ENCODING_BGRA },
	{ "XBGR32", V4L2_PIX_FMT_XBGR32, 1,	MMAL_ENCODING_BGR32 },
	{ "RGB32", V4L2_PIX_FMT_RGB32, 1,	MMAL_ENCODING_RGB32 },
	{ "ARGB32", V4L2_PIX_FMT_ARGB32, 1,	MMAL_ENCODING_ARGB },
	{ "XRGB32", V4L2_PIX_FMT_XRGB32, 1,	MMAL_ENCODING_UNUSED },
	{ "HSV24", V4L2_PIX_FMT_HSV24, 1,	MMAL_ENCODING_UNUSED },
	{ "HSV32", V4L2_PIX_FMT_HSV32, 1,	MMAL_ENCODING_UNUSED },
	{ "Y8", V4L2_PIX_FMT_GREY, 1,		MMAL_ENCODING_UNUSED },
	{ "Y10", V4L2_PIX_FMT_Y10, 1,		MMAL_ENCODING_UNUSED },
	{ "Y12", V4L2_PIX_FMT_Y12, 1,		MMAL_ENCODING_UNUSED },
	{ "Y16", V4L2_PIX_FMT_Y16, 1,		MMAL_ENCODING_UNUSED },
	{ "UYVY", V4L2_PIX_FMT_UYVY, 1,		MMAL_ENCODING_UYVY },
	{ "VYUY", V4L2_PIX_FMT_VYUY, 1,		MMAL_ENCODING_VYUY },
	{ "YUYV", V4L2_PIX_FMT_YUYV, 1,		MMAL_ENCODING_YUYV },
	{ "YVYU", V4L2_PIX_FMT_YVYU, 1,		MMAL_ENCODING_YVYU },
	{ "NV12", V4L2_PIX_FMT_NV12, 1,		MMAL_ENCODING_NV12 },
	{ "NV12M", V4L2_PIX_FMT_NV12M, 2,	MMAL_ENCODING_UNUSED },
	{ "NV21", V4L2_PIX_FMT_NV21, 1,		MMAL_ENCODING_NV21 },
	{ "NV21M", V4L2_PIX_FMT_NV21M, 2,	MMAL_ENCODING_UNUSED },
	{ "NV16", V4L2_PIX_FMT_NV16, 1,		MMAL_ENCODING_UNUSED },
	{ "NV16M", V4L2_PIX_FMT_NV16M, 2,	MMAL_ENCODING_UNUSED },
	{ "NV61", V4L2_PIX_FMT_NV61, 1,		MMAL_ENCODING_UNUSED },
	{ "NV61M", V4L2_PIX_FMT_NV61M, 2,	MMAL_ENCODING_UNUSED },
	{ "NV24", V4L2_PIX_FMT_NV24, 1,		MMAL_ENCODING_UNUSED },
	{ "NV42", V4L2_PIX_FMT_NV42, 1,		MMAL_ENCODING_UNUSED },
	{ "YUV420M", V4L2_PIX_FMT_YUV420M, 3,	MMAL_ENCODING_UNUSED },
	{ "YUV422M", V4L2_PIX_FMT_YUV422M, 3,	MMAL_ENCODING_UNUSED },
	{ "YUV444M", V4L2_PIX_FMT_YUV444M, 3,	MMAL_ENCODING_UNUSED },
	{ "YVU420M", V4L2_PIX_FMT_YVU420M, 3,	MMAL_ENCODING_UNUSED },
	{ "YVU422M", V4L2_PIX_FMT_YVU422M, 3,	MMAL_ENCODING_UNUSED },
	{ "YVU444M", V4L2_PIX_FMT_YVU444M, 3,	MMAL_ENCODING_UNUSED },
	{ "SBGGR8", V4L2_PIX_FMT_SBGGR8, 1,	MMAL_ENCODING_BAYER_SBGGR8 },
	{ "SGBRG8", V4L2_PIX_FMT_SGBRG8, 1,	MMAL_ENCODING_BAYER_SGBRG8 },
	{ "SGRBG8", V4L2_PIX_FMT_SGRBG8, 1,	MMAL_ENCODING_BAYER_SGRBG8 },
	{ "SRGGB8", V4L2_PIX_FMT_SRGGB8, 1,	MMAL_ENCODING_BAYER_SRGGB8 },
	{ "SBGGR10_DPCM8", V4L2_PIX_FMT_SBGGR10DPCM8, 1,	MMAL_ENCODING_UNUSED },
	{ "SGBRG10_DPCM8", V4L2_PIX_FMT_SGBRG10DPCM8, 1,	MMAL_ENCODING_UNUSED },
	{ "SGRBG10_DPCM8", V4L2_PIX_FMT_SGRBG10DPCM8, 1,	MMAL_ENCODING_UNUSED },
	{ "SRGGB10_DPCM8", V4L2_PIX_FMT_SRGGB10DPCM8, 1,	MMAL_ENCODING_UNUSED },
	{ "SBGGR10", V4L2_PIX_FMT_SBGGR10, 1,	MMAL_ENCODING_UNUSED },
	{ "SGBRG10", V4L2_PIX_FMT_SGBRG10, 1,	MMAL_ENCODING_UNUSED },
	{ "SGRBG10", V4L2_PIX_FMT_SGRBG10, 1,	MMAL_ENCODING_UNUSED },
	{ "SRGGB10", V4L2_PIX_FMT_SRGGB10, 1,	MMAL_ENCODING_UNUSED },
	{ "SBGGR10P", V4L2_PIX_FMT_SBGGR10P, 1,	MMAL_ENCODING_BAYER_SBGGR10P },
	{ "SGBRG10P", V4L2_PIX_FMT_SGBRG10P, 1,	MMAL_ENCODING_BAYER_SGBRG10P },
	{ "SGRBG10P", V4L2_PIX_FMT_SGRBG10P, 1,	MMAL_ENCODING_BAYER_SGRBG10P },
	{ "SRGGB10P", V4L2_PIX_FMT_SRGGB10P, 1,	MMAL_ENCODING_BAYER_SRGGB10P },
	{ "SBGGR12", V4L2_PIX_FMT_SBGGR12, 1,	MMAL_ENCODING_UNUSED },
	{ "SGBRG12", V4L2_PIX_FMT_SGBRG12, 1,	MMAL_ENCODING_UNUSED },
	{ "SGRBG12", V4L2_PIX_FMT_SGRBG12, 1,	MMAL_ENCODING_UNUSED },
	{ "SRGGB12", V4L2_PIX_FMT_SRGGB12, 1,	MMAL_ENCODING_UNUSED },
	{ "DV", V4L2_PIX_FMT_DV, 1,		MMAL_ENCODING_UNUSED },
	{ "MJPEG", V4L2_PIX_FMT_MJPEG, 1,	MMAL_ENCODING_UNUSED },
	{ "MPEG", V4L2_PIX_FMT_MPEG, 1,		MMAL_ENCODING_UNUSED },
};
typedef struct
{
   int id;
   char *command;
   char *abbrev;
   char *help;
   int num_parameters;
} COMMAND_LIST;
/// Command ID's and Structure defining our command line options
enum
{
   CommandHelp,
   CommandTimeout,
   CommandWidth,
   CommandHeight,
   CommandQuality,
   CommandEncoding,
   CommandOutput,
   CommandHFlip,  
   CommandVFlip, 
   CommandSharpness, 
   CommandContrast,  
   CommandBrightness,
   CommandSaturation,
   CommandExposure,
   CommandPixelFormat,
   CommandFaceDetection, 
   CommandTimelapse, 
   CommandPreview,
   CommandFullScreen,
   CommandOpacity,
};

static struct
{
   char *format;
   uint32_t encoding;
} encoding_xref[] =
{
   {"jpg", MMAL_ENCODING_JPEG},
   {"bmp", MMAL_ENCODING_BMP},
   {"gif", MMAL_ENCODING_GIF},
   {"png", MMAL_ENCODING_PNG},
   {"ppm", MMAL_ENCODING_PPM},
   {"tga", MMAL_ENCODING_TGA},
   {"h264",MMAL_ENCODING_H264},
};
static int encoding_xref_size = sizeof(encoding_xref) / sizeof(encoding_xref[0]);
static COMMAND_LIST  cmdline_commands[] =
{
//common set
   { CommandHelp,    "-help",       "?",  "This help information", 0 },
   { CommandTimeout, "-timeout",    "t",  "Time (in ms) before takes picture and shuts down (if not specified, set to 5s)", 1 },
   { CommandWidth,   "-width",      "w",  "Set image width <size>", 1 },
   { CommandHeight,  "-height",     "h",  "Set image height <size>", 1 },
   { CommandQuality, "-quality",    "q",  "Set jpeg quality <0 to 100>", 1 },
   { CommandEncoding,"-encoding",   "e",  "Encoding to use for output file (jpg, bmp, gif, png, h264)", 1},
   { CommandOutput,  "-output",     "o",  "Output filename <filename> (to write to stdout, use '-o -'). If not specified, no file is saved", 1 },
   {CommandHFlip,       "-hflip",     "hf", "Set horizontal flip", 0},
   {CommandVFlip,       "-vflip",     "vf", "Set vertical flip", 0},
   {CommandSharpness,   "-sharpness", "sh", "Set image sharpness (0 to 6)",  1},
   {CommandContrast,    "-contrast",  "co", "Set image contrast (0 to 4)",  1},
   {CommandBrightness,  "-brightness","br", "Set image brightness (0 to 65535)",  1},
   {CommandSaturation,  "-saturation","sa", "Set image saturation (0 to 4)", 1},
   {CommandExposure,    "-exposure",  "ex", "Set exposure mode (on off)", 1},
   {CommandPixelFormat,    "-pixelFormat",  "pixfmt", "Set the pixel format('UYVY', RGB565')", 1},
   {CommandFaceDetection,    "-faceDetection", "facedet","Set face detection enable", 1},
  // { CommandTimelapse,"-timelapse", "tl", "Timelapse mode. Takes a picture every <t>ms. %d == frame number (Try: -o img_%04d.jpg)", 1},
   { CommandPreview,	"-preview",	"p",  "Preview window settings <'x,y,w,h'>", 1 },
   { CommandFullScreen,	"-fullscreen",	"fs", "Fullscreen preview mode", 0 },
   { CommandOpacity,	"-opacity",	"op", "Preview window opacity (0-255)", 1},
};
static int cmdline_commands_size = sizeof(cmdline_commands) / sizeof(cmdline_commands[0]);
typedef struct
{
   int timeout;                        /// Time taken before frame is grabbed and app then shuts down. Units are milliseconds
   int width;                          /// Requested width of image
   int height;                         /// requested height of image
   char *filename;                     /// filename of output file
   char *linkname;                     /// filename of output file
   int quality;                        /// JPEG quality setting (1-100)
   int demoInterval;                   /// Interval between camera settings changes
   MMAL_FOURCC_T encoding;             /// Encoding to use for the output file.
   int numExifTags;                    /// Number of supplied tags
   int enableExifTags;                 /// Enable/Disable EXIF tags in output
   int timelapse;                      /// Delay between each picture in timelapse mode. If 0, disable timelapse
   int fullResPreview;                 /// If set, the camera preview port runs at capture resolution. Reduces fps.
   int frameNextMethod;                /// Which method to use to advance to next frame
   int useGL;                          /// Render preview using OpenGL
   int glCapture;                      /// Save the GL frame-buffer instead of camera output
   int burstCaptureMode;               /// Enable burst mode
   int datetime;                       /// Use DateTime instead of frame#
   int timestamp;                      /// Use timestamp instead of frame#
   int restart_interval;               /// JPEG restart interval. 0 for none.
   uint32_t arducamCtlName; 
   int arducamCtlValue; 
   int fullscreen;		// 0 is use previewRect, non-zero to use full screen
   MMAL_RECT_T preview_window;	// Destination rectangle for the preview window.      
} RASPISTILL_STATE;
uint8_t saveImageBurst = 0;
uint8_t saveImageFlag = 0;
uint8_t f = 0;
uint8_t  do_set_control = 0;
struct device dev = {0};

struct v4l2_format_info *v4l2_format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pixel_formats); ++i) {
		if (pixel_formats[i].fourcc == fourcc)
			return &pixel_formats[i];
	}

	return NULL;
}
struct v4l2_format_info *v4l2_format_by_name(char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pixel_formats); ++i) {
		if (strcasecmp(pixel_formats[i].name, name) == 0)
			return &pixel_formats[i];
	}

	return NULL;
}
char *v4l2_format_name(unsigned int fourcc)
{
	struct v4l2_format_info *info;
	static char name[5];
	unsigned int i;

	info = v4l2_format_by_fourcc(fourcc);
	if (info)
		return info->name;

	for (i = 0; i < 4; ++i) {
		name[i] = fourcc & 0xff;
		fourcc >>= 8;
	}

	name[4] = '\0';
	return name;
}

static void video_set_buf_type(struct device *dev, enum v4l2_buf_type type)
{
	dev->type = type;
}

static bool video_has_valid_buf_type(struct device *dev)
{
	return (int)dev->type != -1;
}

static int video_querycap(struct device *dev, unsigned int *capabilities)
{
	struct v4l2_capability cap;
	unsigned int caps;
	int ret;

	memset(&cap, 0, sizeof cap);
	ret = ioctl(dev->fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0)
		return 0;

	caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS
	     ? cap.device_caps : cap.capabilities;
	*capabilities = caps;

	return 0;
}

static int cap_get_buf_type(unsigned int capabilities)
{
	if (capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
		return V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	} else if (capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) {
		return V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	} else if (capabilities & V4L2_CAP_VIDEO_CAPTURE) {
		return  V4L2_BUF_TYPE_VIDEO_CAPTURE;
	} else if (capabilities & V4L2_CAP_VIDEO_OUTPUT) {
		return V4L2_BUF_TYPE_VIDEO_OUTPUT;
	} else {
		print("Device supports neither capture nor output.\n");
		return -EINVAL;
	}

	return 0;
}

static void video_close(struct device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_planes; i++)
		free(dev->pattern[i]);

	free(dev->buffers);
	if (dev->opened)
		close(dev->fd);
}

static int set_control(struct device *dev)
{
	int ret;
	struct v4l2_control old;
	old.id = dev->arducamCtlName;;
	old.value = dev->arducamCtlValue;
	ret = ioctl(dev->fd, VIDIOC_S_CTRL, &old);
	return ret;
}
//video_set_format(&dev, state.width, state.height, dev.pixelformat)
static int video_set_format(struct device *dev)
{
	struct v4l2_format fmt;
	int ret;
	memset(&fmt, 0, sizeof fmt);
	fmt.type = dev->type;
	fmt.fmt.pix.width = dev->width;
	fmt.fmt.pix.height = dev->height;
	fmt.fmt.pix.pixelformat = dev->pixelformat;
	ret = ioctl(dev->fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		print("Unable to set format: %s (%d).\n", strerror(errno),
			errno);
		return ret;
	}
	print("Video format set: %s (%08x) %ux%u\r\n",\
		v4l2_format_name(fmt.fmt.pix.pixelformat), fmt.fmt.pix.pixelformat,
		fmt.fmt.pix.width, fmt.fmt.pix.height);

	return 0;
}
static int buffer_export(int v4l2fd, enum v4l2_buf_type bt, int index, int *dmafd, unsigned int *vcsm_hdl)
{
	struct v4l2_exportbuffer expbuf;
	unsigned int vcsm_handle;

	memset(&expbuf, 0, sizeof(expbuf));
	expbuf.type = bt;
	expbuf.index = index;
	if (ioctl(v4l2fd, VIDIOC_EXPBUF, &expbuf))
	{
		print("Failed to EXPBUF\n");
		return -1;
	}
	*dmafd = expbuf.fd;

	//print("Importing DMABUF %d into VCSM...\n", expbuf.fd);
	vcsm_handle = vcsm_import_dmabuf(expbuf.fd, "V4L2 buf");
	if (!vcsm_handle){
		print("Importing DMABUF %d into VCSM...\n", expbuf.fd);
		print("...done. Failed\n");
	}
	*vcsm_hdl = vcsm_handle;
	return vcsm_handle ? 0 : -1;
}

static int video_buffer_mmap(struct device *dev, struct buffer *buffer,
			     struct v4l2_buffer *v4l2buf)
{
	unsigned int length;
	unsigned int offset;
	unsigned int i;

	for (i = 0; i < dev->num_planes; i++) {
		if (video_is_mplane(dev)) {
			length = v4l2buf->m.planes[i].length;
			offset = v4l2buf->m.planes[i].m.mem_offset;
		} else {
			length = v4l2buf->length;
			offset = v4l2buf->m.offset;
		}

		buffer->mem[i] = mmap(0, length, PROT_READ | PROT_WRITE, MAP_SHARED,
				      dev->fd, offset);
		if (buffer->mem[i] == MAP_FAILED) {
			print("Unable to map buffer %u/%u: %s (%d)\n",
			       buffer->idx, i, strerror(errno), errno);
			return -1;
		}

		buffer->size[i] = length;
		buffer->padding[i] = 0;
	}

	return 0;
}

static int video_buffer_munmap(struct device *dev, struct buffer *buffer)
{
	unsigned int i;
	int ret;

	for (i = 0; i < dev->num_planes; i++) {
		ret = munmap(buffer->mem[i], buffer->size[i]);
		if (ret < 0) {
			print("Unable to unmap buffer %u/%u: %s (%d)\n",
			       buffer->idx, i, strerror(errno), errno);
		}

		buffer->mem[i] = NULL;
	}

	return 0;
}

static int video_alloc_buffers(struct device *dev)
{
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	struct v4l2_requestbuffers rb;
	struct v4l2_buffer buf;
	struct buffer *buffers;
	unsigned int i;
	int ret;
	memset(&rb, 0, sizeof rb);  
	rb.count = 3;
	rb.type = dev->type;
	rb.memory = dev->memtype;
	ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rb);
	if (ret < 0) {
		print("Unable to request buffers: %s (%d).\n", strerror(errno),errno);
		return ret;
	}
	buffers = malloc(rb.count * sizeof (buffers[0]));
	dev->mmal_pool = mmal_pool_create(rb.count , 0);
	if (!dev->mmal_pool) {
		print("Failed to create pool\n");
		return -1;
	}
	if (buffers == NULL)
		return -ENOMEM;
	/* Map the buffers. */
	for (i = 0; i < rb.count; ++i) {
		memset(&buf, 0, sizeof buf);
		memset(planes, 0, sizeof planes);
		buf.index = i;
		buf.type = dev->type;
		buf.memory = dev->memtype;
		buf.length = VIDEO_MAX_PLANES;
		buf.m.planes = planes;
		ret = ioctl(dev->fd, VIDIOC_QUERYBUF, &buf);
		if (ret < 0) {
			print("Unable to query buffer %u: %s (%d).\n", i,
				strerror(errno), errno);
			return ret;
		}
		buffers[i].idx = i;
		ret = video_buffer_mmap(dev, &buffers[i], &buf);
		if (ret < 0)
			return ret;
		if (!buffer_export(dev->fd, dev->type, i, &buffers[i].dma_fd, &buffers[i].vcsm_handle))
		{
			dev->can_zero_copy = MMAL_TRUE;
		//	print("Exported buffer %d to dmabuf %d, vcsm handle %u\n", i, buffers[i].dma_fd, buffers[i].vcsm_handle);
		}
		else
		{
			if (dev->can_zero_copy)
			{
				print("Some buffer exported whilst others not. HELP!\n");
				dev->can_zero_copy = MMAL_FALSE;
			}
		}
		if (dev->mmal_pool) {
			MMAL_BUFFER_HEADER_T *mmal_buf;
			mmal_buf = mmal_queue_get(dev->mmal_pool->queue);
			if (!mmal_buf) {
				print("Failed to get a buffer from the pool. Queue length %d\n", mmal_queue_length(dev->mmal_pool->queue));
				return -1;
			}
			mmal_buf->user_data = &buffers[i];
			if (dev->can_zero_copy)
				mmal_buf->data = (uint8_t*)vcsm_vc_hdl_from_hdl(buffers[i].vcsm_handle);
			else
				mmal_buf->data = buffers[i].mem[0];
			mmal_buf->alloc_size = buf.length;
			buffers[i].mmal = mmal_buf;
			/* Put buffer back in the pool */
			mmal_buffer_header_release(mmal_buf);
		}
	}

	dev->timestamp_type = buf.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK;
	dev->buffers = buffers;
	dev->nbufs = rb.count;
	return 0;
}
static int video_free_buffers(struct device *dev)
{
	struct v4l2_requestbuffers rb;
	unsigned int i;
	int ret;

	if (dev->nbufs == 0)
		return 0;

	for (i = 0; i < dev->nbufs; ++i) {
		switch (dev->memtype) {
		case V4L2_MEMORY_MMAP:
			if (dev->buffers[i].vcsm_handle)
			{
				//print("Releasing vcsm handle %u\n", dev->buffers[i].vcsm_handle);
				vcsm_free(dev->buffers[i].vcsm_handle);
			}
			if (dev->buffers[i].dma_fd)
			{
				//print("Closing dma_buf %d\n", dev->buffers[i].dma_fd);
				close(dev->buffers[i].dma_fd);
			}
			ret = video_buffer_munmap(dev, &dev->buffers[i]);
			if (ret < 0)
				return ret;
			break;
		default:
			break;
		}
	}

	memset(&rb, 0, sizeof rb);
	rb.count = 0;
	rb.type = dev->type;
	rb.memory = dev->memtype;

	ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rb);
	if (ret < 0) {
		print("Unable to release buffers: %s (%d).\n",
			strerror(errno), errno);
		return ret;
	}
	free(dev->buffers);
	dev->nbufs = 0;
	dev->buffers = NULL;
	return 0;
}
static int video_queue_buffer(struct device *dev, int index)
{
	struct v4l2_buffer buf;
	int ret;
	memset(&buf, 0, sizeof buf);
	buf.index = index;
	buf.type = dev->type;
	buf.memory = dev->memtype;
	ret = ioctl(dev->fd, VIDIOC_QBUF, &buf);
	if (ret < 0)
		print("Unable to queue buffer: %s (%d).\n",
			strerror(errno), errno);
	return ret;
}
static int video_enable(struct device *dev, int enable)
{
	int type = dev->type;
	int ret;

	ret = ioctl(dev->fd, enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		print("Unable to %s streaming: %s (%d).\n",
			enable ? "start" : "stop", strerror(errno), errno);
		return ret;
	}

	return 0;
}
static int video_queue_all_buffers(struct device *dev)
{
	unsigned int i;
	int ret;
	/* Queue the buffers. */
	for (i = 0; i < dev->nbufs; ++i) {
		ret = video_queue_buffer(dev, i);
		if (ret < 0)
			return ret;
	}
	return 0;
}
static void isp_ip_cb(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	struct device *dev = (struct device *)port->userdata;
	unsigned int i;
	for (i = 0; i < dev->nbufs; i++) {
		if (dev->buffers[i].mmal == buffer) {
			video_queue_buffer(dev, dev->buffers[i].idx);
			mmal_buffer_header_release(buffer);
			buffer = NULL;
			break;
		}
	}
	if (buffer) {
		mmal_buffer_header_release(buffer);
	}
}
static void * h264_save_thread(void *arg)
{
	struct device *dev = (struct device *)arg;
	MMAL_BUFFER_HEADER_T *buffer;
	MMAL_STATUS_T status;
	uint32_t bytes_written;
	while (!dev->thread_quit)
	{
		//Being lazy and using a timed wait instead of setting up a
		//mechanism for skipping this when destroying the thread
		buffer = mmal_queue_timedwait(dev->save_queue, 500);
		if (!buffer)
			continue;
		if (dev->image_fd)
		{
			bytes_written = fwrite(buffer->data, 1, buffer->length, dev->image_fd);
			fflush(dev->image_fd);

			if (bytes_written != (uint32_t)(buffer->length))
			{
				printf("Failed to write buffer data (%d from %d)- aborting\r\n", bytes_written, buffer->length);
			}
		}
		buffer->length = 0;
		status = mmal_port_send_buffer(dev->encoder->output[0], buffer);
		if(status != MMAL_SUCCESS)
		{
			print("mmal_port_send_buffer failed on buffer %p, status %d", buffer, status);
		}
	}
	return NULL;
}
static void * image_save_thread(void *arg)
{
	struct device *dev = (struct device *)arg;
	MMAL_BUFFER_HEADER_T *buffer;
	MMAL_STATUS_T status;
	unsigned int bytes_written;
	while (!dev->thread_quit)
	{
		buffer = mmal_queue_timedwait(dev->save_queue, 2000);
		if (!buffer)
			continue;
		if(dev->saveImageFlag){
			//printf("buffer length: %d\r\n",buffer->length);
			if(dev->image_fd == NULL){
				dev->image_fd = fopen(dev->imageName,"wb");
			}
			if(dev->image_fd){
				if(buffer->flags != 4){ 
					bytes_written = fwrite(buffer->data, 1, buffer->length, dev->image_fd);
					fflush(dev->image_fd);
					if (bytes_written != buffer->length)
					{
						print("Failed to write buffer data (%d from %d)- aborting", bytes_written, buffer->length);
					}
				}else{
					bytes_written = fwrite(buffer->data, 1, buffer->length, dev->image_fd);
					fflush(dev->image_fd);
					if (bytes_written != buffer->length)
					{
						print("Failed to write buffer data (%d from %d)- aborting", bytes_written, buffer->length);
					}
					fclose(dev->image_fd);
					printf("\r\nSaving image data to %s successfully.\r\n",dev->imageName);
					dev->saveImageFlag = 0;
				}
			}	
		}
		buffer->length = 0;
		status = mmal_port_send_buffer(dev->encoder->output[0], buffer);
		if(status != MMAL_SUCCESS)
		{
			print("mmal_port_send_buffer failed on buffer %p, status %d", buffer, status);
		}
	}
	return NULL;
}
static void encoder_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	struct device *dev = (struct device *)port->userdata;
	if (port->is_enabled){
	mmal_queue_put(dev->save_queue, buffer);
	}
	else{
		mmal_buffer_header_release(buffer);
	}	

}

static void buffers_to_isp2(struct device *dev)
{
	MMAL_BUFFER_HEADER_T *buffer;
	while ((buffer = mmal_queue_get(dev->isp_output_pool2->queue)) != NULL)
	{
		mmal_port_send_buffer(dev->isp->output[1], buffer);
	}
}
static void buffers_to_isp(struct device *dev)
{
	MMAL_BUFFER_HEADER_T *buffer;
	while ((buffer = mmal_queue_get(dev->isp_output_pool->queue)) != NULL)
	{
		mmal_port_send_buffer(dev->isp->output[0], buffer);
	}
}

static void isp_output2_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	struct device *dev = (struct device*)port->userdata;
	if (dev->render)
	{
		MMAL_BUFFER_HEADER_T *out = mmal_queue_get(dev->render_pool->queue);
		if (out)
		{
			mmal_buffer_header_replicate(out, buffer);
			mmal_port_send_buffer(dev->render->input[0], out);
		}
	}
	mmal_buffer_header_release(buffer);
}

static void isp_output_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	struct device *dev = (struct device*)port->userdata;
	if(dev->saveImageFlag  ||  dev->encoderFmt.encoding == MMAL_ENCODING_H264 ){
		if (dev->encoder)
		{
			MMAL_BUFFER_HEADER_T *out = mmal_queue_get(dev->encode_pool->queue);
			if (out)
			{ 
				mmal_buffer_header_replicate(out, buffer);
				mmal_port_send_buffer(dev->encoder->input[0], out);
			}else{
				printf("get error\r\n ");
			}
		}
	}
	if (dev->render && (dev->isp_output1Flag == 0))
	{
		MMAL_BUFFER_HEADER_T *out = mmal_queue_get(dev->render_pool->queue);
		if (out)
		{
			mmal_buffer_header_replicate(out, buffer);
			mmal_port_send_buffer(dev->render->input[0], out);
		}
	}
	mmal_buffer_header_release(buffer);
}
static void render_encoder_input_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	struct device *dev = (struct device*)port->userdata;
	mmal_buffer_header_release(buffer);
		buffers_to_isp(dev);
	if(dev->isp_output1Flag)
		buffers_to_isp2(dev);
}
#define LOG_DEBUG print

#define DEFAULT_PREVIEW_LAYER 3
static MMAL_STATUS_T create_render_component(struct device *dev){
	MMAL_STATUS_T status;
	MMAL_PORT_T *isp_output = NULL;
	if(dev->isp_output1Flag){
		isp_output = dev->isp->output[1];
	}
	else{
		isp_output = dev->isp->output[0];
	}
	status = mmal_component_create("vc.ril.video_render", &dev->render);
	if (status != MMAL_SUCCESS)
	{
		printf("Unable to create render component\r\n");
		return -1;
	}

    //preview windows setting 
    MMAL_DISPLAYREGION_T param;
	param.hdr.id = MMAL_PARAMETER_DISPLAYREGION;
	param.hdr.size = sizeof(MMAL_DISPLAYREGION_T);
	param.set = MMAL_DISPLAY_SET_LAYER;
	param.layer = DEFAULT_PREVIEW_LAYER;
	param.set |= MMAL_DISPLAY_SET_ALPHA;
	param.alpha = dev->opacity;
	if (dev->fullscreen)
	{
		param.set |= MMAL_DISPLAY_SET_FULLSCREEN;
		param.fullscreen = 1;
	}
	else
	{
		param.set |= (MMAL_DISPLAY_SET_DEST_RECT | MMAL_DISPLAY_SET_FULLSCREEN);
		param.fullscreen = 0;
		param.dest_rect = dev->preview_window;
	}

	status = mmal_port_parameter_set(dev->render->input[0], &param.hdr);
	if (status != MMAL_SUCCESS && status != MMAL_ENOSYS)
	{
		printf("unable to set preview port parameters (%u)\r\n", status);
	}
	status = mmal_format_full_copy(dev->render->input[0]->format, isp_output->format);
	dev->render->input[0]->buffer_num = 3;
	if (status == MMAL_SUCCESS){
		status = mmal_port_format_commit(dev->render->input[0]);	
		if (status != MMAL_SUCCESS)
		{
				printf("mmal_port_format_commit error \r\n");
			return -1;
		}
	}
	status += mmal_port_parameter_set_boolean(dev->render->input[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
	if (status != MMAL_SUCCESS)
	{
			printf("set error \r\n");
		return -1;
	}
	//创建内内存池给dev->render->input[0]
	dev->render_pool = mmal_port_pool_create(dev->render->input[0], dev->render->input[0]->buffer_num,\
											 dev->render->input[0]->buffer_size);
	if(!dev->render_pool)
	{
		print("Failed to create render pool\n");
		return -1;
	}
	dev->render->input[0]->userdata = (struct MMAL_PORT_USERDATA_T *)dev;
	status = mmal_port_enable(dev->render->input[0], render_encoder_input_callback);
	if (status != MMAL_SUCCESS){
		printf("failed to mmal_port_enable render-input[0]");
		return -1;
	}
	return status;
}
static MMAL_STATUS_T create_isp_component(struct device *dev){
	MMAL_PORT_T *isp_input = NULL, *isp_output = NULL, *isp_output2 = NULL;
	MMAL_STATUS_T status;
	const struct v4l2_format_info *info;
	struct v4l2_format fmt;
	int ret = 0;
	status = mmal_component_create("vc.ril.isp", &dev->isp);
	if (status != MMAL_SUCCESS)
	{
		printf("Unable to create isp component\r\n");
		return -1;
	}
	status = mmal_component_enable(dev->isp);
	if (status  != MMAL_SUCCESS)
	{
		printf("Unable to enable video isp component\r\n");
		return -1;
	}
	isp_input = dev->isp->input[0];
    isp_output = dev->isp->output[0];
	isp_output2 = dev->isp->output[1];
	//获取图像接口属性
	memset(&fmt, 0, sizeof fmt);
	fmt.type = dev->type;
	ret = ioctl(dev->fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0) {
		print("Unable to get format: %s (%d).\n", strerror(errno),
			errno);
		return ret;
	}
	info = v4l2_format_by_fourcc(fmt.fmt.pix.pixelformat);
	if (!info || info->mmal_encoding == MMAL_ENCODING_UNUSED)
	{
		print("Unsupported encoding\n");
		return -1;
	}
	isp_input->format->encoding = info->mmal_encoding;
	isp_input->format->es->video.crop.width = fmt.fmt.pix.width;
	isp_input->format->es->video.crop.height = fmt.fmt.pix.height;
	isp_input->format->es->video.width = (isp_input->format->es->video.crop.width+31) & ~31;
	isp_input->format->es->video.height = (fmt.fmt.pix.height+15) & ~15;	
	isp_input->buffer_num = 3;

	status = mmal_port_format_commit(isp_input);
	if (status != MMAL_SUCCESS)
	{
		print("Commit failed\n");
		return -1;
	}
	if (mmal_port_parameter_set_boolean(isp_input, MMAL_PARAMETER_ZERO_COPY, dev->can_zero_copy) != MMAL_SUCCESS)
	{
		print("Failed to set zero copy\n");
		return -1;
	}

	//isp output
	mmal_format_copy(isp_output->format, isp_input->format);
	isp_output->format->encoding = MMAL_ENCODING_I420;
	dev->isp_output1Flag = 0;
	if(isp_output->format->es->video.crop.width > 1920){ // use isp2 to display
		//isp output2
		mmal_format_copy(isp_output2->format, isp_input->format);
		isp_output2->format->encoding = MMAL_ENCODING_I420;
		while(isp_output2->format->es->video.crop.width > 1920)
		{
			isp_output2->format->es->video.crop.width >>= 1;
			isp_output2->format->es->video.crop.height >>= 1;
		}
		isp_output2->buffer_num = 3;
		status = mmal_port_format_commit(isp_output2);
		if (status != MMAL_SUCCESS)
		{
			print("ISP output2 commit failed\r\n");
			return -1;
		}
		status = mmal_port_parameter_set_boolean(isp_output2, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
		if (status != MMAL_SUCCESS){
			printf("mmal_port_parameter_set_boolean error\r\n");
			return -1;
		}	
		dev->isp_output_pool2 = mmal_port_pool_create(isp_output2, isp_output2->buffer_num, isp_output2->buffer_size);
		if(!dev->isp_output_pool2)
		{
			print("Failed to create pool\r\n");
			return -1;
		}
		dev->isp_output1Flag = 1;
	}
		isp_output->buffer_num =3;
		status = mmal_port_format_commit(isp_output);
		if (status != MMAL_SUCCESS)
		{
			print("ISP output commit failed\n");
			return -1;
		}
		status = mmal_port_parameter_set_boolean(isp_output, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
		dev->isp_output_pool = mmal_port_pool_create(isp_output, isp_output->buffer_num, isp_output->buffer_size);
		if(!dev->isp_output_pool)
		{
			print("Failed to create pool\n");
			return -1;
		}
		isp_output->userdata = (struct MMAL_PORT_USERDATA_T *)dev;
		status = mmal_port_enable(isp_output, isp_output_callback);
		if (status != MMAL_SUCCESS)
			return -1;
		buffers_to_isp(dev);
		if(dev->isp_output1Flag){
			isp_output2->userdata = (struct MMAL_PORT_USERDATA_T *)dev;
			status = mmal_port_enable(isp_output2, isp_output2_callback);
			if (status != MMAL_SUCCESS)
				return -1;
			buffers_to_isp2(dev);
		}
		return status;
}


static MMAL_STATUS_T create_image_encoder_component(struct device *dev)
{
	MMAL_STATUS_T status;
	MMAL_PORT_T  *isp_output = dev->isp->output[0];
	MMAL_PORT_T *encoder_input = NULL, *encoder_output = NULL;	
	VCOS_STATUS_T vcos_status;
	status = mmal_component_create("vc.ril.image_encode", &dev->encoder);
	if(status != MMAL_SUCCESS)
	{
		print("Failed to create encoder");
		return -1;
	}
	encoder_input  = dev->encoder->input[0];
	encoder_output = dev->encoder->output[0];
	status = mmal_format_full_copy(encoder_input->format, isp_output->format);
	if(status != MMAL_SUCCESS)
	{
		return -1;
	}
	status = mmal_port_format_commit(encoder_input);
	if(status != MMAL_SUCCESS)
	{
		return -1;
	}
	//printf("encoder_input->format.height %d,encoder_input->format.width %d\r\n",encoder_input->format->es->video.height,encoder_input->format->es->video.width);
	
	status = mmal_port_parameter_set_boolean(encoder_input, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
	if(status != MMAL_SUCCESS)
	{
		print("Fail to set MMAL_PARAMETER_ZERO_COPY\r\n");
		return -1;
	} 
   // We want same format on input and output
   mmal_format_copy(encoder_output->format, encoder_input->format);
   // Specify out output format
   encoder_output->format->encoding = dev->encoderFmt.encoding;
   encoder_output->buffer_size = encoder_output->buffer_size_recommended*3 + 1024;
   if (encoder_output->buffer_size < encoder_output->buffer_size_min)
		encoder_output->buffer_size = encoder_output->buffer_size_min;
    encoder_output->buffer_num =encoder_output->buffer_num_recommended;
	if (encoder_output->buffer_num < encoder_output->buffer_num_min)
		encoder_output->buffer_num = encoder_output->buffer_num_min;
 //printf("encoder_output->format.height %d,encoder_output->format.width %d\r\n",encoder_output->format->es->video.height,encoder_output->format->es->video.width);
   // Commit the port changes to the output port
   status = mmal_port_format_commit(encoder_output);
   if(status != MMAL_SUCCESS)
	{
		print("Fail to mmal_port_format_commit(encoder_output)\r\n");
		return -1;
	} 
    dev->save_queue = mmal_queue_create();
	if(!dev->save_queue)
	{
		print("Failed to create queue\n");
		return -1;
	}
    vcos_status = vcos_thread_create(&dev->save_thread, "image-save-thread",
				NULL, image_save_thread, dev);
	if(vcos_status != VCOS_SUCCESS)
	{
		print("Failed to create save thread\n");
		return -1;
	}
	status = mmal_port_parameter_set_boolean(encoder_output, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
	if(status != MMAL_SUCCESS)
	{
		print("Failed to set zero copy\n");
		return -1;
	}
	// Set the JPEG quality level
  status = mmal_port_parameter_set_uint32(encoder_output, MMAL_PARAMETER_JPEG_Q_FACTOR, dev->encoderFmt.quality);
  if (status != MMAL_SUCCESS)
  {
     printf("Unable to set JPEG quality\r\n");
     return -1;
  }
  // Set the JPEG restart interval
 status = mmal_port_parameter_set_uint32(encoder_output, MMAL_PARAMETER_JPEG_RESTART_INTERVAL, dev->restart_interval);
 if (dev->restart_interval && status != MMAL_SUCCESS)
 {
    printf("Unable to set JPEG restart interval\r\n");
    return -1;
 }
 // Set up any required thumbnail
{
   MMAL_PARAMETER_THUMBNAIL_CONFIG_T param_thumb = {{MMAL_PARAMETER_THUMBNAIL_CONFIGURATION, sizeof(MMAL_PARAMETER_THUMBNAIL_CONFIG_T)}, 0, 0, 0, 0};
   //if ( dev->thumbnailConfig.enable &&
   //      dev->thumbnailConfig.width > 0 && dev->thumbnailConfig.height > 0 )
   {
      // Have a valid thumbnail defined
      param_thumb.enable = 1;
      param_thumb.width = 64;//dev->thumbnailConfig.width;
      param_thumb.height = 48;//dev->thumbnailConfig.height;
      param_thumb.quality = 35;//dev->thumbnailConfig.quality;

   }
   status = mmal_port_parameter_set(dev->encoder->control, &param_thumb.hdr);
}
	status = mmal_component_enable(dev->encoder);
	if(status != MMAL_SUCCESS)
	{
		print("Failed to enable\n");
		return -1;
	}

	//print("Create pool of %d buffers of size %d for encode ip\n", encoder_input->buffer_num, 0);
	dev->encode_pool = mmal_port_pool_create(encoder_input, encoder_input->buffer_num, encoder_input->buffer_size);
	if(!dev->encode_pool)
	{
		print("Failed to create encode ip pool\n");
		return -1;
	}
	encoder_input->userdata = (struct MMAL_PORT_USERDATA_T *)dev;
	status = mmal_port_enable(encoder_input, render_encoder_input_callback);
	if (status != MMAL_SUCCESS)
	{
		print("Failed to enable encoder_input\n");
		return -1;
	}
	encoder_output->userdata = (void*)dev;
	status = mmal_port_enable(encoder_output, encoder_buffer_callback);
	if(status != MMAL_SUCCESS)
	{
		print("Failed to enable encoder_output port\n");
		return -1;
	}
	status = mmal_component_enable(dev->encoder);
	if(status != MMAL_SUCCESS)
	{
		print("Failed to enable\n");
		return -1;
	}
	unsigned int i;
	dev->output_pool = mmal_port_pool_create(encoder_output, encoder_output->buffer_num, encoder_output->buffer_size);
	if(!dev->output_pool)
	{
		print("Failed to create pool\n");
		return -1;
	}
	for(i=0; i<encoder_output->buffer_num; i++)
	{
		MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(dev->output_pool->queue);

		if (!buffer)
		{
			print("Where'd my buffer go?!\n");
			return -1;
		}
		status = mmal_port_send_buffer(encoder_output, buffer);
		if(status != MMAL_SUCCESS)
		{
			print("mmal_port_send_buffer failed on buffer %p, status %d\n", buffer, status);
			return -1;
		}
	}
return 0;
}
#define VCOS_ALIGN_DOWN(p,n) (((ptrdiff_t)(p)) & ~((n)-1))
#define VCOS_ALIGN_UP(p,n) VCOS_ALIGN_DOWN((ptrdiff_t)(p)+(n)-1,(n))

static MMAL_STATUS_T create_video_encoder_component(struct device *dev)
{
	MMAL_STATUS_T status;
	MMAL_PORT_T  *isp_output = dev->isp->output[0];
	MMAL_PORT_T *encoder_input = NULL, *encoder_output = NULL;	
	VCOS_STATUS_T vcos_status;
	// Max bitrate we allow for recording
	int MAX_BITRATE_MJPEG = 25000000;   // 25Mbits/s
	int MAX_BITRATE_LEVEL4 = 25000000;  // 25Mbits/s
	int MAX_BITRATE_LEVEL42 = 62500000; // 62.5Mbits/s
	//int width = dev->width;
    //int height = dev->height; 
	status = mmal_component_create("vc.ril.video_encode", &dev->encoder);
	if(status != MMAL_SUCCESS)
	{
		printf("Failed to create encoder");
		return -1;
	}
	
	VIDEO_ENCODER_STATE video_state;
    default_video_status(&video_state);
	VIDEO_ENCODER_STATE *state = &video_state;
	encoder_input  = dev->encoder->input[0];
	encoder_output = dev->encoder->output[0];
	encoder_input->buffer_num = 6;
	status = mmal_format_full_copy(encoder_input->format, isp_output->format);
	if(status != MMAL_SUCCESS)
	{
		return -1;
	}
	status = mmal_port_format_commit(encoder_input);
	if(status != MMAL_SUCCESS)
	{
		return -1;
	}
	status = mmal_port_parameter_set_boolean(encoder_input, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
	if(status != MMAL_SUCCESS)
	{
		printf("Fail to set MMAL_PARAMETER_ZERO_COPY\r\n");
		return -1;
	}

   // We want same format on input and output
   mmal_format_copy(encoder_output->format, encoder_input->format);
    encoder_output->format->encoding = state->encoding;
    if (state->encoding == MMAL_ENCODING_H264) {
        if (state->level == MMAL_VIDEO_LEVEL_H264_4) {
            if (state->bitrate > MAX_BITRATE_LEVEL4) {
                fprintf(stderr, "Bitrate too high: Reducing to 25MBit/s\n");
                state->bitrate = MAX_BITRATE_LEVEL4;
            }
        } else {
            if (state->bitrate > MAX_BITRATE_LEVEL42) {
                fprintf(stderr, "Bitrate too high: Reducing to 62.5MBit/s\n");
                state->bitrate = MAX_BITRATE_LEVEL42;
            }
        }
    } else if (state->encoding == MMAL_ENCODING_MJPEG) {
        if (state->bitrate > MAX_BITRATE_MJPEG) {
            fprintf(stderr, "Bitrate too high: Reducing to 25MBit/s\n");
            state->bitrate = MAX_BITRATE_MJPEG;
        }
    }
    encoder_output->format->bitrate = state->bitrate;
    if (state->encoding == MMAL_ENCODING_H264)
        encoder_output->buffer_size = encoder_output->buffer_size_recommended;
    else
        encoder_output->buffer_size = 256 << 12;
    if (encoder_output->buffer_size < encoder_output->buffer_size_min)
        encoder_output->buffer_size = encoder_output->buffer_size_min;

    encoder_output->buffer_num = encoder_output->buffer_num_recommended;

    if (encoder_output->buffer_num < encoder_output->buffer_num_min)
        encoder_output->buffer_num = encoder_output->buffer_num_min;

    // We need to set the frame rate on output to 0, to ensure it gets
    // updated correctly from the input framerate when port connected
    encoder_output->format->es->video.frame_rate.num = 0;
    encoder_output->format->es->video.frame_rate.den = 1;
    // Commit the port changes to the output port
    status = mmal_port_format_commit(encoder_output);

    if (status != MMAL_SUCCESS) {
        printf("Unable to set format on video encoder output port");
        return -1;
    }

  dev->save_queue = mmal_queue_create();
	if(!dev->save_queue)
	{
		print("Failed to create queue\n");
		return -1;
	}
    vcos_status = vcos_thread_create(&dev->save_thread, "h264-save-thread",
				NULL, h264_save_thread, dev);
	if(vcos_status != VCOS_SUCCESS)
	{
		print("Failed to create save thread\n");
		return -1;
	}
	if(dev->imageName == NULL){
		dev->imageName = "arducam.h264";
	}
	if( strcmp(dev->imageName,"stdout") == 0){
		dev->image_fd = stdout;
	}else{
	dev->image_fd = fopen(dev->imageName,"wb");
	}
    // Set the rate control paramete
    if (state->encoding == MMAL_ENCODING_H264) {
        MMAL_PARAMETER_VIDEO_PROFILE_T param;
        param.hdr.id = MMAL_PARAMETER_PROFILE;
        param.hdr.size = sizeof(param);

        param.profile[0].profile = state->profile;

        param.profile[0].level = state->level;
        status = mmal_port_parameter_set(encoder_output, &param.hdr);
        if (status != MMAL_SUCCESS) {
            printf("Unable to set H264 profile\r\n");
            return -1;
        }
    }

    if (mmal_port_parameter_set_boolean(encoder_input,
                                        MMAL_PARAMETER_VIDEO_IMMUTABLE_INPUT,
                                        state->immutableInput) != MMAL_SUCCESS) {
        printf("Unable to set immutable input flag\r\n");
        // Continue rather than abort..
    }

    if (state->encoding == MMAL_ENCODING_H264) {
        // set INLINE HEADER flag to generate SPS and PPS for every IDR if requested
        if (mmal_port_parameter_set_boolean(
                encoder_output, MMAL_PARAMETER_VIDEO_ENCODE_INLINE_HEADER,
                state->bInlineHeaders) != MMAL_SUCCESS) {
            printf("failed to set INLINE HEADER FLAG parameters\r\n");
            // Continue rather than abort..
        }

        // set flag for add SPS TIMING
        if (mmal_port_parameter_set_boolean(encoder_output,
                                            MMAL_PARAMETER_VIDEO_ENCODE_SPS_TIMING,
                                            state->addSPSTiming) != MMAL_SUCCESS) {
            printf("failed to set SPS TIMINGS FLAG parameters\r\n");
            // Continue rather than abort..
        }

        // set INLINE VECTORS flag to request motion vector estimates
        if (mmal_port_parameter_set_boolean(
                encoder_output, MMAL_PARAMETER_VIDEO_ENCODE_INLINE_VECTORS,
                state->inlineMotionVectors) != MMAL_SUCCESS) {
            printf("failed to set INLINE VECTORS parameters\r\n");
            // Continue rather than abort..
        }
    }
	status = mmal_port_parameter_set_boolean(encoder_output, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
	if(status != MMAL_SUCCESS)
	{
		print("Failed to set zero copy\n");
		return -1;
	}	
	status = mmal_component_enable(dev->encoder);
	if(status != MMAL_SUCCESS)
	{
		print("Failed to enable\n");
		return -1;
	}

	encoder_input->userdata = (struct MMAL_PORT_USERDATA_T *)dev;
	status = mmal_port_enable(encoder_input, render_encoder_input_callback);
	if (status != MMAL_SUCCESS)
	{
		print("Failed to enable encoder_input\n");
		return -1;
	}
	encoder_output->userdata = (void*)dev;
	status = mmal_port_enable(encoder_output, encoder_buffer_callback);
	if(status != MMAL_SUCCESS)
	{
		print("Failed to enable encoder_output port\n");
		return -1;
	}
	status = mmal_component_enable(dev->encoder);
	if(status != MMAL_SUCCESS)
	{
		print("Failed to enable\n");
		return -1;
	}
	unsigned int i;
	dev->encode_pool = mmal_port_pool_create(encoder_input, encoder_input->buffer_num, encoder_input->buffer_size);
	if(!dev->encode_pool)
	{
		print("Failed to create encode ip pool\n");
		return -1;
	}
	//Create encoder output buffers
	dev->output_pool = mmal_port_pool_create(encoder_output, encoder_output->buffer_num, encoder_output->buffer_size);
	if(!dev->output_pool)
	{
		print("Failed to create pool\n");
		return -1;
	}
	for(i=0; i<encoder_output->buffer_num; i++)
	{
		MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(dev->output_pool->queue);

		if (!buffer)
		{
			print("Where'd my buffer go?!\n");
			return -1;
		}
		status = mmal_port_send_buffer(encoder_output, buffer);
		if(status != MMAL_SUCCESS)
		{
			print("mmal_port_send_buffer failed on buffer %p, status %d\n", buffer, status);
			return -1;
		}
	//	print("Sent buffer %p\n", buffer);
	}
return 0;

}
static int setup_mmal(struct device *dev)
{
	create_isp_component(dev);
	create_render_component(dev);
	if(dev->encoderFmt.encoding == MMAL_ENCODING_H264){
		create_video_encoder_component(dev);
	}


	
return 0;
}

static int enable_mmal_ip(struct device *dev)
{
	MMAL_STATUS_T status;

	if (mmal_port_parameter_set_boolean(dev->isp->input[0], MMAL_PARAMETER_ZERO_COPY, dev->can_zero_copy) != MMAL_SUCCESS)
	{
		print("Failed to set zero copy\n");
		return -1;
	}
	dev->isp->input[0]->userdata= (void*)dev;
	status = mmal_port_enable(dev->isp->input[0], isp_ip_cb);
	if (status != MMAL_SUCCESS)
	{
		print("ISP input enable failed\n");
		return -1;
	}
	return 0;
}

static void destroy_mmal(struct device *dev)
{
	//FIXME: Clean up everything properly
	dev->thread_quit = 1;
	vcos_thread_join(&dev->save_thread, NULL);
}

//static int video_do_capture(struct device *dev, unsigned int nframes,
//	unsigned int skip, unsigned int delay, const char *pattern,
//	int do_requeue_last, int do_queue_late, enum buffer_fill_mode fill)
static int video_do_capture(struct device *dev)
{
	struct v4l2_buffer buf;
	int ret;
	//int dropped_frames = 0;
	int run_state =1;
	time_t begin = 0;
	int frameCnt = 0;
	/* Start streaming. */
	ret = video_enable(dev, 1);
	if (ret < 0)
		goto done;
	 while(run_state){
		
		fd_set fds[3];
		fd_set *rd_fds = &fds[0]; /* for capture */
		fd_set *ex_fds = &fds[1]; /* for capture */
		fd_set *wr_fds = &fds[2]; /* for output */
		struct timeval tv;
		int r;
		if (rd_fds) {
			FD_ZERO(rd_fds);
			FD_SET(dev->fd, rd_fds);
		}
		if (ex_fds) {
			FD_ZERO(ex_fds);
			FD_SET(dev->fd, ex_fds);
		}
		if (wr_fds) {
			FD_ZERO(wr_fds);
			FD_SET(dev->fd, wr_fds);
		}
		/* Timeout. */
		tv.tv_sec = 10;
		tv.tv_usec = 0;
		r = select(dev->fd + 1, rd_fds, wr_fds, ex_fds, &tv);
		if (-1 == r) {
				if (EINTR == errno)
						continue;
				errno_exit("select");
		}
		if (0 == r) {
				fprintf(stderr, "select timeout\n");
				exit(EXIT_FAILURE);
		}
        if (rd_fds && FD_ISSET(dev->fd, rd_fds)) {
			int queue_buffer = 1;
			/* Dequeue a buffer. */
			memset(&buf, 0, sizeof buf);
			buf.type = dev->type;
			ret = ioctl(dev->fd, VIDIOC_DQBUF, &buf);
			if (ret < 0) {
				print("Unable to dequeue buffer: %s (%d).\n",strerror(errno), errno);
			}
			if (dev->mmal_pool) {
				MMAL_BUFFER_HEADER_T *mmal;
				MMAL_STATUS_T status;
				while ((mmal = mmal_queue_get(dev->mmal_pool->queue)) && !mmal->user_data) {
					print("Discarding MMAL buffer %p as not mapped\n", mmal);
				}
				if (!mmal) {
					print("Failed to get MMAL buffer\n");
				} else {
					/* Need to wait for MMAL to be finished with the buffer before returning to V4L2 */
					queue_buffer = 0;
					if (((struct buffer*)mmal->user_data)->idx != buf.index) {
						print("Mismatch in expected buffers. V4L2 gave idx %d, MMAL expecting %d\n",
							buf.index, ((struct buffer*)mmal->user_data)->idx);
					}
					mmal->length = buf.length;	
					//发送图像数据到ISP-INPUT[0]
 					if(dev->stopStreaming){
						if(dev->imageName){
							usleep(1000*1000);
							dev->saveImageFlag =1;	
							if(dev->imageName != NULL){
									create_image_encoder_component(dev);
								}
							status = mmal_port_send_buffer(dev->isp->input[0], mmal); 
							if (status != MMAL_SUCCESS)
								print("mmal_port_send_buffer failed %d\n", status);
							while(dev->saveImageFlag){usleep(10);}
						}


						break;
					}else{
						status = mmal_port_send_buffer(dev->isp->input[0], mmal); 
						if (status != MMAL_SUCCESS)
							print("mmal_port_send_buffer failed %d\n", status);
					}
					frameCnt++;
					if(time(NULL) - begin >= 1){
            		 	printf("\r[Framerate]: %02d fps.", 
                    	frameCnt);
						fflush(stdout);
            			frameCnt = 0;
            			begin = time(NULL);
        			 }
					
					
					
					
				}
			}
			if (!queue_buffer){
				continue;
			}
			ret = video_queue_buffer(dev, buf.index);
			if (ret < 0) {
				print("Unable to requeue buffer: %s (%d).\n",
					strerror(errno), errno);
				goto done;
			}
		}
    }
	/* Stop streaming. */
	ret = video_enable(dev, 0);
	if (ret < 0)
		return ret;
	return video_free_buffers(dev);
done:
	video_close(dev);
	return video_free_buffers(dev);
}


#define V4L_BUFFERS_DEFAULT	8
#define V4L_BUFFERS_MAX		32

#define OPT_ENUM_FORMATS	256
#define OPT_ENUM_INPUTS		257
#define OPT_SKIP_FRAMES		258
#define OPT_NO_QUERY		259
#define OPT_SLEEP_FOREVER	260
#define OPT_USERPTR_OFFSET	261
#define OPT_REQUEUE_LAST	262
#define OPT_STRIDE		263
#define OPT_FD			264
#define OPT_TSTAMP_SRC		265
#define OPT_FIELD		266
#define OPT_LOG_STATUS		267
#define OPT_BUFFER_SIZE		268
#define OPT_PREMULTIPLIED	269
#define OPT_QUEUE_LATE		270
#define OPT_DATA_PREFIX		271

int arducamVideoInit(struct device *dev){
	int ret;
	memset(dev, 0, sizeof *dev);
	dev->fd = -1;
	dev->memtype = V4L2_MEMORY_MMAP;
	dev->buffers = NULL;
	dev->type = (enum v4l2_buf_type)-1;
	dev->fd = open("/dev/video0", O_RDWR);
	if (dev->fd < 0) {
		print("Error opening device.\n");
		return dev->fd;
	}
	print("Device /dev/video0) opened.\n");
	dev->opened = 1;
	ret = video_querycap(dev, &capabilities);
	if (ret < 0)
		return 1;
	ret = cap_get_buf_type(capabilities);
	if (ret < 0)
		return 1;
	if (!video_has_valid_buf_type(dev))
		video_set_buf_type(dev, ret);
	return 0;
}
int arducamMmalConfig(struct device *dev){
	bcm_host_init();
	if(setup_mmal(dev))
	{
		printf("setup_mmal failed\r\n");
		return 1;
	}
	if (video_alloc_buffers(dev)){
		video_close(dev);
		return 1;
	}
	if (enable_mmal_ip(dev))
	{
		video_close(dev);
		return 1;
	}
	if (video_queue_all_buffers(dev)) {
		video_close(dev);
		return 1;
	}
	return 0;
}


int arducamStartStreaming(struct device *dev){
if (video_do_capture(dev) < 0) {
		video_close(dev);
		return 1;
	}
	return 0;
}

void raspicli_display_help(const COMMAND_LIST *commands, const int num_commands)
{
   int i;

   vcos_assert(commands);

   if (!commands)
      return;

   for (i = 0; i < num_commands; i++)
   {
      fprintf(stdout, "-%s, -%s\t: %s\n", commands[i].abbrev,
              commands[i].command, commands[i].help);
   }
}
/**
 * Display usage information for the application to stdout
 *
 * @param app_name String to display as the application name
 */
static void application_help_message(char *app_name)
{
   fprintf(stdout, "Runs camera for specific time, and take JPG capture at end if requested\n\n");
   fprintf(stdout, "usage: %s [options]\n\n", app_name);
   fprintf(stdout, "Image parameter commands\n\n");

   raspicli_display_help(cmdline_commands, cmdline_commands_size);
   return;
}
/**
 * Convert a string from command line to a comand_id from the list
 *
 * @param commands Array of command to check
 * @param num_command Number of commands in the array
 * @param arg String to search for in the list
 * @param num_parameters Returns the number of parameters used by the command
 *
 * @return command ID if found, -1 if not found
 *
 */
int raspicli_get_command_id(const COMMAND_LIST *commands, const int num_commands, const char *arg, int *num_parameters)
{
   int command_id = -1;
   int j;

   vcos_assert(commands);
   vcos_assert(num_parameters);
   vcos_assert(arg);

   if (!commands || !num_parameters || !arg)
      return -1;

   for (j = 0; j < num_commands; j++)
   {
      if (!strcmp(arg, commands[j].command) ||
            !strcmp(arg, commands[j].abbrev))
      {
         // match
         command_id = commands[j].id;
         *num_parameters = commands[j].num_parameters;
         break;
      }
   }

   return command_id;
}
/**
 * Parse the incoming command line and put resulting parameters in to the state
 *
 * @param argc Number of arguments in command line
 * @param argv Array of pointers to strings from command line
 * @param state Pointer to state structure to assign any discovered parameters to
 * @return non-0 if failed for some reason, 0 otherwise
 */
static int parse_cmdline(int argc, char **argv,struct device *dev)
{
   // Parse the command line arguments.
   // We are looking for --<something> or -<abbreviation of something>

   int valid = 1;
   int i;

   for (i = 1; i < argc && valid; i++)
   {
      int command_id, num_parameters;

      if (!argv[i])
         continue;

      if (argv[i][0] != '-')
      {
         valid = 0;
         continue;
      }

      // Assume parameter is valid until proven otherwise
      valid = 1;
      command_id = raspicli_get_command_id(cmdline_commands, cmdline_commands_size, &argv[i][1], &num_parameters);
      // If we found a command but are missing a parameter, continue (and we will drop out of the loop)
      if (command_id != -1 && num_parameters > 0 && (i + 1 >= argc) )
         continue;

      //  We are now dealing with a command line option
      switch (command_id)
      {
		case CommandHelp:
		{
			application_help_message("./arducamstill");
			exit(0);
			break;
		}
		case CommandTimeout: // Time to run viewfinder for before taking picture, in seconds
		{
			if (sscanf(argv[i + 1], "%d", &dev->runTime) == 1)
			{
				i++;
			}
			else
				valid = 0;
			break;
		}
		case CommandWidth: // Width > 0
			{
				if (sscanf(argv[i + 1], "%u", &dev->width) == 1)
				{
					i++;
				}
				else
					valid = 0;
				break;
			}
   		case CommandHeight: // Height > 0
		   {
			   if (sscanf(argv[i + 1], "%u", &dev->height) == 1)
				{
					i++;
				}
				else
					valid = 0;
				break;
		   }
		case CommandQuality: // Quality = 1-100
			{
			if (sscanf(argv[i + 1], "%u", &dev->encoderFmt.quality) == 1)
			{
				if (dev->encoderFmt.quality > 100)
				{
				fprintf(stderr, "Setting max quality = 100\n");
				dev->encoderFmt.quality= 100;
				}
				i++;
			}
			else
				valid = 0;
			break;
			}
		case CommandEncoding :
			{
				int len = strlen(argv[i + 1]);
				valid = 0;

				if (len)
				{
					int j;
					for (j=0; j<encoding_xref_size; j++)
					{
					if (strcmp(encoding_xref[j].format, argv[i+1]) == 0)
					{
						dev->encoderFmt.encoding= encoding_xref[j].encoding;
						valid = 1;
						i++;
						break;
					}
					}
				}
				break;
			}
   		case CommandOutput:  // output filename
		   {
			   int len = strlen(argv[i+1]);
				if (len)
				{
					dev->imageName = malloc(len + 10);
					vcos_assert(dev->imageName);
					if (dev->imageName)
					strncpy(dev->imageName, argv[i + 1], len+1);
					i++;
				}
				else
					valid = 0;
				break;
		   }
		case CommandHFlip:
		{
			if (sscanf(argv[i + 1], "%u", &dev->arducamCtlValue) == 1)
				{
					i++;
					dev->arducamCtlName = horizontal_flip;
					do_set_control =1;
				}
				else
					valid = 0;
				break;
		}
		case CommandVFlip:
		{
			if (sscanf(argv[i + 1], "%u", &dev->arducamCtlValue) == 1)
				{
					i++;
					dev->arducamCtlName = vertical_flip;
					do_set_control =1;
				}
				else
					valid = 0;
				break;
		}
		case CommandSharpness:
		{
			if (sscanf(argv[i + 1], "%u", &dev->arducamCtlValue) == 1)
				{
					i++;
					dev->arducamCtlName = sharpness;
					do_set_control =1;
				}
				else
					valid = 0;
				break;
		}
		case CommandContrast:
		{
			if (sscanf(argv[i + 1], "%u", &dev->arducamCtlValue) == 1)
				{
					i++;
					dev->arducamCtlName = sharpness;
					do_set_control =1;
				}
				else
					valid = 0;
				break;
		}
		case CommandBrightness:
		{
			if (sscanf(argv[i + 1], "%u", &dev->arducamCtlValue) == 1)
				{
					i++;
					dev->arducamCtlName = brightness;
					do_set_control =1;
				}
				else
					valid = 0;
				break;
		}
		case CommandSaturation:
		{
			if (sscanf(argv[i + 1], "%u", &dev->arducamCtlValue) == 1)
				{
					i++;
					dev->arducamCtlName = saturation;
					do_set_control =1;
				}
				else
					valid = 0;
				break;
		}
		case CommandExposure:
		{
			if (sscanf(argv[i + 1], "%u", &dev->arducamCtlValue) == 1)
				{
					i++;
					dev->arducamCtlName = exposure;
					do_set_control =1;
				}
				else
					valid = 0;
				break;
		}
		case CommandPixelFormat:
		{
			printf("%s\r\n",argv[i+1]);
			dev->info = v4l2_format_by_name(argv[i+1]);
			if (dev->info == NULL) {
				print("Unsupported video format '%s'\n", argv[i+1]);
				return 1;
			}
			dev->pixelformat = dev->info->fourcc;
			i++;
			break;
			
		}
		case CommandFaceDetection:
		{
			if (sscanf(argv[i + 1], "%u", &dev->arducamCtlValue) == 1)
				{
					i++;
					dev->arducamCtlName = face_detection;
					do_set_control =1;
				}
				else
					valid = 0;
				break;
		}
		case CommandOpacity: // Define preview window opacity
				if (sscanf(argv[i + 1], "%u", &dev->opacity) != 1)
					dev->opacity = 255;
				else
					i++;
				break;
		case CommandPreview: // Preview window
			{
				int tmp;

				tmp = sscanf(argv[i + 1], "%d,%d,%d,%d",
					&dev->preview_window.x, &dev->preview_window.y,
					&dev->preview_window.width, &dev->preview_window.height);
				// Failed to get any window parameters, so revert to full screen
				if (tmp != 4)
					dev->fullscreen = 1;
				else
					dev->fullscreen = 0;

				i++;

				break;
			}
		default:
		valid = 0;
    }
   }

   if (!valid)
   {
      fprintf(stderr, "Invalid command line option (%s)\n", argv[i-1]);
      return 1;
   }

   return 0;
}
static void default_status(struct device *dev)
{
   if (!dev)
   {
      vcos_assert(0);
      return;
   }
   dev->runTime = -1;// replaced with 5000ms later if unset
   dev->encoderFmt.quality = 80;
   dev->encoderFmt.encoding = MMAL_ENCODING_JPEG;
   dev->width = 1920;
   dev->height = 1080;
   dev->imageName = NULL;
   dev->pixelformat = V4L2_PIX_FMT_UYVY;
   dev->opacity = 255;
   dev->fullscreen = 1;
}
static void * runTime_thread(void *arg){
	struct device *dev = (struct device *)arg;
	if(dev->runTime){
		usleep(1000*dev->runTime);
		dev->stopStreaming =1;
	}
	return NULL;
}



int main(int argc, char *argv[])
{
    if(arducamVideoInit(&dev)){
		printf("arducamVideoInit failed.\r\n");
	}
	default_status(&dev);
	if (parse_cmdline(argc, argv, &dev))
	{
		exit(-1);
	}
	if (dev.runTime == -1)
     	 dev.runTime = 5000;
	if (dev.height && dev.width) {
		if (video_set_format(&dev) < 0) {
			video_close(&dev);
			return 1;
		}
	}
    //init mmal
	if(arducamMmalConfig(&dev)){
		printf("arducamMmalConfig failed.\r\n");
	}
	//Add support for control
	if( do_set_control){
		set_control(&dev);
	}
	//create time thread
    VCOS_STATUS_T vcos_status = vcos_thread_create(&dev.runTime_thread, "time-thread",
				NULL, runTime_thread, &dev);
	if(vcos_status != VCOS_SUCCESS)
	{
		print("Failed to create save thread\n");
		return -1;
	}
	if(arducamStartStreaming(&dev)){
		printf("arducamStartStreaming failed.\r\n");	
	}
	destroy_mmal(&dev);
	video_close(&dev);
	return 0;
}

