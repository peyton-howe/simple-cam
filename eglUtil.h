#pragma once

#include <map>

#include <libcamera/libcamera.h>

#include <libdrm/drm_fourcc.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <epoxy/egl.h>
#include <epoxy/gl.h>

int setupEGL(char const *name, int x, int y, int width, int height);
void makeBuffer(int fd, libcamera::StreamConfiguration const &cfg, libcamera::FrameBuffer *buffer, int camera_num);
void displayframe(int width, int height);
