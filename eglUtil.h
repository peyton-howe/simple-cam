#pragma once

#include <map>

#include <epoxy/egl.h>
#include <epoxy/gl.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>


int setupEGL(char const *name, int width, int height);
