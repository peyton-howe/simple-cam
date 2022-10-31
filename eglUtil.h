#pragma once

#include <map>

#include <libcamera/libcamera.h>

#include <libdrm/drm_fourcc.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <epoxy/egl.h>
#include <epoxy/gl.h>

//#include "glhelp.h"

struct Buffer
{
	int fd;
	GLuint texture;
};

int setupEGL(char const *name, int width, int height);
void makeBuffer(int fd, libcamera::StreamConfiguration const &cfg, libcamera::FrameBuffer *buffer, int camera_num);
