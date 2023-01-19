#pragma once

#include <map>

#include <libcamera/libcamera.h>

#include <libdrm/drm_fourcc.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <epoxy/egl.h>
#include <epoxy/gl.h>

struct EGLUtil
{
	EGLDisplay display;
	EGLSurface surface;
	EGLContext context;
	EGLConfig *config;
	
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

int setupEGL(char const *name, int x, int y, int width, int height);
void makeBuffer(int fd, libcamera::StreamConfiguration const &cfg, libcamera::FrameBuffer *buffer, int camera_num);
void displayEGL(int width, int height);
void gl_setup();
