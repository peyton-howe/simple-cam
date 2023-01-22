#pragma once

#include <map>

#include <libcamera/libcamera.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

#include <epoxy/egl.h>
#include <epoxy/gl.h>

struct EGLUtil
{
	EGLDisplay display;
	EGLSurface surface;
	EGLContext context;
	EGLConfig  config;  //X11 config (maybe can clean this up??)
	EGLConfig *configs; //DRM config
	
	EGLint major, minor;
	EGLint vid;
	EGLint num_configs;
	
	GLuint FramebufferName;
	GLuint FramebufferName2;
};

static const EGLint ctx_attribs[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
};

static const EGLint conf_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RED_SIZE, 1,
	EGL_GREEN_SIZE, 1,
	EGL_BLUE_SIZE, 1,
	EGL_ALPHA_SIZE, 0,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_NONE
};

struct X11Util
{
	Display *display;
	Atom wm_delete_window;
	Window window;
};

struct DRMUtil
{
	drmModeRes *resources;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	drmModeModeInfo mode;
	drmModeCrtc *crtc;
	int fd = -1;
	uint32_t conId;
	uint32_t crtcId;
	int crtcIdx;
	uint32_t planeId;
};

struct GBMUtil
{
	gbm_device *device;
	gbm_surface *surface;
	uint32_t fb;
	gbm_bo *previousBo = NULL;
	uint32_t previousFb;
};

int makeWindow(char const *name, int x, int y, int width, int height);
void makeBuffer(int fd, libcamera::StreamConfiguration const &cfg, libcamera::FrameBuffer *buffer, int camera_num);
void displayFrame(int width, int height);
void gbmClean();
void cleanup();
