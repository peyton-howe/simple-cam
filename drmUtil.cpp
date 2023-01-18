#include "drmEglUtil.h"

#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <gbm.h>

#include <epoxy/egl.h>
#include <epoxy/gl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <iostream>
#include <unistd.h>

int drmfd_;
uint32_t conId_;
uint32_t crtcId_;
int crtcIdx_;
uint32_t planeId_;
uint32_t fb;

EGLDisplay egl_display_;
EGLSurface egl_surface_;
EGLContext egl_context_;

EGLint egl_major, egl_minor;
EGLint vid;

bool first_time_ = true;

GLuint FramebufferName;
GLuint FramebufferName2;

drmModeRes *drm_resources_;
drmModeConnector *drm_connector_ = NULL;
drmModeEncoder *drm_encoder_ = NULL;
drmModeModeInfo drm_mode_;
drmModeCrtc *crtc;

gbm_device *gbm_dev_;
gbm_surface *gbm_surface_;

EGLConfig *egl_config_;
EGLint num_configs;

static struct gbm_bo *previousBo = NULL;
static uint32_t previousFb;
#define ERRSTR strerror(errno)

static GLint compile_shader(GLenum target, const char *source)
{
	GLuint s = glCreateShader(target);
	glShaderSource(s, 1, (const GLchar **)&source, NULL);
	glCompileShader(s);

	GLint ok;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

	if (!ok)
	{
		GLchar *info;
		GLint size;

		glGetShaderiv(s, GL_INFO_LOG_LENGTH, &size);
		info = (GLchar *)malloc(size);

		glGetShaderInfoLog(s, size, NULL, info);
		throw std::runtime_error("failed to compile shader: " + std::string(info) + "\nsource:\n" +
								 std::string(source));
	}

	return s;
}

static GLint link_program(GLint vs, GLint fs)
{
	GLint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);

	GLint ok;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		/* Some drivers return a size of 1 for an empty log.  This is the size
		 * of a log that contains only a terminating NUL character.
		 */
		GLint size;
		GLchar *info = NULL;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &size);
		if (size > 1)
		{
			info = (GLchar *)malloc(size);
			glGetProgramInfoLog(prog, size, NULL, info);
		}

		throw std::runtime_error("failed to link: " + std::string(info ? info : "<empty log>"));
	}

	return prog;
}

static void gl_setup()
{
	float w_factor = 1920 / (float)1920;
	float h_factor = 1080 / (float)1080;
	float max_dimension = std::max(w_factor, h_factor);
	w_factor /= max_dimension;
	h_factor /= max_dimension;
	char vs[256];
	snprintf(vs, sizeof(vs),
			 "attribute vec4 pos;\n"
			 "varying vec2 texcoord;\n"
			 "\n"
			 "void main() {\n"
			 "  gl_Position = pos;\n"
			 "  texcoord.x = pos.x / %f + 0.5;\n"
			 "  texcoord.y = 0.5 - pos.y / %f;\n"
			 "}\n",
			 2.0 * w_factor, 2.0 * h_factor);
	vs[sizeof(vs) - 1] = 0;
	GLint vs_s = compile_shader(GL_VERTEX_SHADER, vs);
	const char *fs = "#extension GL_OES_EGL_image_external : enable\n"
					 "precision mediump float;\n"
					 "uniform samplerExternalOES s;\n"
					 "varying vec2 texcoord;\n"
					 "void main() {\n"
					 "  gl_FragColor = texture2D(s, texcoord);\n"
					 "}\n";
	GLint fs_s = compile_shader(GL_FRAGMENT_SHADER, fs);
	GLint prog = link_program(vs_s, fs_s);

	glUseProgram(prog);

	static const float verts[] = { -w_factor, -h_factor, w_factor, -h_factor, w_factor, h_factor, -w_factor, h_factor };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glEnableVertexAttribArray(0);
	glGenTextures(1, &FramebufferName);
	glGenTextures(1, &FramebufferName2);
}

static drmModeConnector *getConnector(drmModeRes *resources)
{
    for (int i = 0; i < resources->count_connectors; i++)
    {
        drmModeConnector *connector = drmModeGetConnector(drmfd_, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED)
        {
            return connector;
        }
        drmModeFreeConnector(connector);
    }

    return NULL;
}

static drmModeEncoder *findEncoder(drmModeConnector *connector)
{
if (connector->encoder_id)
    {
        return drmModeGetEncoder(drmfd_, connector->encoder_id);
    }
    return NULL;
}

static int matchConfigToVisual(EGLDisplay display, EGLint visualId, EGLConfig *configs, int count)
{
    EGLint id;
    EGLint blue_size, red_size, green_size, alpha_size;
    for (int i = 0; i < count; ++i)
    {
        if (!eglGetConfigAttrib(display, configs[i], EGL_NATIVE_VISUAL_ID, &id))
            continue;
        if (id == visualId)
            return i;
	    
	eglGetConfigAttrib(egl_display_, configs[i], EGL_RED_SIZE, &red_size);
        eglGetConfigAttrib(egl_display_, configs[i], EGL_GREEN_SIZE, &green_size);
        eglGetConfigAttrib(egl_display_, configs[i], EGL_BLUE_SIZE, &blue_size);
        eglGetConfigAttrib(egl_display_, configs[i], EGL_ALPHA_SIZE, &alpha_size);	
        
        char gbm_format_str[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        memcpy(gbm_format_str, &id, sizeof(EGLint));
        printf("  %d-th GBM format: %s;  sizes(RGBA) = %d,%d,%d,%d,\n",
               i, gbm_format_str, red_size, green_size, blue_size, alpha_size); 
    }
    return -1;
}

void findPlane()
{
	drmModePlaneResPtr planes;
	drmModePlanePtr plane;
	unsigned int i;
	unsigned int j;

	planes = drmModeGetPlaneResources(drmfd_);
	if (!planes)
		throw std::runtime_error("drmModeGetPlaneResources failed: " + std::string(ERRSTR));

	try
	{
		for (i = 0; i < planes->count_planes; ++i)
		{
			plane = drmModeGetPlane(drmfd_, planes->planes[i]);
			if (!planes)
				throw std::runtime_error("drmModeGetPlane failed: " + std::string(ERRSTR));

			if (!(plane->possible_crtcs & (1 << crtcIdx_)))
			{
				drmModeFreePlane(plane);
				continue;
			}

			for (j = 0; j < plane->count_formats; ++j)
			{
				if (plane->formats[j] == GBM_FORMAT_XRGB8888)
				{
					break;
				}
			}

			if (j == plane->count_formats)
			{
				drmModeFreePlane(plane);
				continue;
			}

			planeId_ = plane->plane_id;

			drmModeFreePlane(plane);
			break;
		}
	}
	catch (std::exception const &e)
	{
		drmModeFreePlaneResources(planes);
		throw;
	}

	drmModeFreePlaneResources(planes);
}

static void gbmSwapBuffers(EGLDisplay *display, EGLSurface *surface)
{
	eglSwapBuffers(*display, *surface);
	struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbm_surface_);
	uint32_t handle = gbm_bo_get_handle(bo).u32;
	uint32_t pitch = gbm_bo_get_stride(bo);
	uint32_t fb;
	
	uint32_t offsets[4] =
		{ 0, pitch * drm_mode_.vdisplay, pitch * drm_mode_.vdisplay + (pitch / 2) * (drm_mode_.vdisplay / 2) };
	uint32_t pitches[4] = { pitch, pitch / 2, pitch / 2 };
	uint32_t handles[4] = { handle, handle, handle };
	
	//drmModeAddFB(drmfd_, drm_mode_.hdisplay, drm_mode_.vdisplay, 24, 32, pitch, handle, &fb);
	drmModeAddFB2(drmfd_, drm_mode_.hdisplay, drm_mode_.vdisplay, GBM_FORMAT_XRGB8888,
	              handles, pitches, offsets, &fb, 0);
	drmModeSetCrtc(drmfd_, crtc->crtc_id, fb, 0, 0, &conId_, 1, &drm_mode_);
	
	//if (drmModeSetPlane(drmfd_, planeId_, crtcId_, fb, 0, 0, 0, drm_mode_.hdisplay, drm_mode_.vdisplay, 0, 0,
						//drm_mode_.hdisplay << 16, drm_mode_.vdisplay << 16))
		//throw std::runtime_error("drmModeSetPlane failed: " + std::string(ERRSTR));
				
	if (previousBo)
	{
		drmModeRmFB(drmfd_, previousFb);
		gbm_surface_release_buffer(gbm_surface_, previousBo);
	}
	previousBo = bo;
	previousFb = fb;
}

static void gbmClean()
{
    // set the previous crtc
    drmModeSetCrtc(drmfd_, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y, &conId_, 1, &crtc->mode);
    drmModeFreeCrtc(crtc);

    if (previousBo)
    {
        drmModeRmFB(drmfd_, previousFb);
        gbm_surface_release_buffer(gbm_surface_, previousBo);
    }

    gbm_surface_destroy(gbm_surface_);
    gbm_device_destroy(gbm_dev_);
}


static int init_EGL(void)
{
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	static const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	egl_display_ = eglGetDisplay((EGLNativeDisplayType)gbm_dev_);
	if (!egl_display_)
		throw std::runtime_error("eglGetDisplay() failed");

	if (!eglInitialize(egl_display_, &egl_major, &egl_minor))
		throw std::runtime_error("failed to initialize\n");

	//printf("Using display %p with EGL version %d.%d\n", egl_display_, major, minor);
	//printf("EGL Version \"%s\"\n", eglQueryString(egl_display_, EGL_VERSION));
	//printf("EGL Vendor \"%s\"\n", eglQueryString(egl_display_, EGL_VENDOR));
	printf("EGL Extensions \"%s\"\n", eglQueryString(egl_display_, EGL_EXTENSIONS));

	if (!eglBindAPI(EGL_OPENGL_ES_API))
		throw std::runtime_error("failed to bind api EGL_OPENGL_ES_API\n");
	
	if(!eglGetConfigs(egl_display_, NULL, 0, &num_configs) || num_configs < 1)
		throw std::runtime_error("cannot get any configs\n");
		
	egl_config_ = (EGLConfig*)malloc(num_configs * sizeof(EGLConfig));
		
	if (!eglChooseConfig(egl_display_, config_attribs, egl_config_, num_configs, &num_configs))
		throw std::runtime_error("couldn't get an EGL visual config");
		
	int configIndex = matchConfigToVisual(egl_display_, GBM_FORMAT_XRGB8888, egl_config_, num_configs);
	if (configIndex < 0){
		eglTerminate(egl_display_);
		gbm_surface_destroy(gbm_surface_);
		gbm_device_destroy(gbm_dev_);
		throw std::runtime_error("Failed to find matching EGL config!\n");
	}
	
	egl_context_ = eglCreateContext(egl_display_, egl_config_[configIndex], EGL_NO_CONTEXT, context_attribs);
	if (egl_context_ == EGL_NO_CONTEXT)
	{
		eglTerminate(egl_display_);
        gbmClean();
		throw std::runtime_error("failed to create context\n");
	}

	egl_surface_ = eglCreateWindowSurface(egl_display_, egl_config_[configIndex], gbm_surface_, NULL);
	if (egl_surface_ == EGL_NO_SURFACE)
	{
		eglDestroyContext(egl_display_, egl_context_);
        eglTerminate(egl_display_);
        gbmClean();
		throw std::runtime_error("failed to create egl surface\n");
	}
	
	free(egl_config_);

	// We have to do eglMakeCurrent in the thread where it will run, but we must do it
	// here temporarily so as to get the maximum texture size.
	eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context_);
	int max_texture_size = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
	//max_image_width_ = max_image_height_ = max_texture_size;
	// This "undoes" the previous eglMakeCurrent.
	eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	//printf("GL Extensions: \"%s\"\n", glGetString(GL_EXTENSIONS));
	eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_);

	glViewport(0, 0, drm_mode_.hdisplay, drm_mode_.vdisplay);
	return 0;
}

int setupEGL(char const *name, int x, int y, int width, int height)
{	
	//drmfd_ = drmOpen("vc4", NULL);
	//we have to try card0 and card1 to see which is valid. fopen will work on both, so...
	drmfd_ = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    
	if ((drm_resources_ = drmModeGetResources(drmfd_)) == NULL) // if we have the right device we can get it's resources
		{
		std::cout << "/dev/dri/card0 does not have DRM resources, using card1\n";
		drmfd_ = open("/dev/dri/card1", O_RDWR | O_CLOEXEC); // if not, try the other one: (1)
		drm_resources_ = drmModeGetResources(drmfd_);
		}
	else
		std::cout << "using /dev/dri/card0\n";


	if (!drmIsMaster(drmfd_))
		throw std::runtime_error("DRM preview unavailable - not master");

	drm_connector_ = getConnector(drm_resources_);
	if (!drm_connector_) // we could be fancy and listen for hotplug events and wait for connector..
	{
		drmModeFreeResources(drm_resources_);
		throw std::runtime_error("no connected connector!\n");
	}
	
	conId_ = drm_connector_->connector_id;
	for (int i = 0; i < drm_connector_->count_modes; i++) {
		drm_mode_ = drm_connector_->modes[i];
		printf("resolution: %ix%i %i\n", drm_mode_.hdisplay, drm_mode_.vdisplay, drm_mode_.vrefresh);
		if (drm_mode_.hdisplay == 1920 && drm_mode_.vdisplay == 1080 && drm_mode_.vrefresh == 60) //set display to 1080p 60Hz
			break;
	}
	//drm_mode_ = drm_connector_->modes[0]; // array of resolutions and refresh rates supported by this display
	printf("resolution: %ix%i\n", drm_mode_.hdisplay, drm_mode_.vdisplay);
		
	drm_encoder_ = findEncoder(drm_connector_);
	if (drm_encoder_ == NULL)
	{
		drmModeFreeConnector(drm_connector_);
		drmModeFreeResources(drm_resources_);
		throw std::runtime_error("Unable to get encoder\n");
	}
	
	crtc = drmModeGetCrtc(drmfd_, drm_encoder_->crtc_id);
	crtcId_ = crtc->crtc_id;
	if (drm_resources_->count_crtcs <= 0)
		throw std::runtime_error("drm: no crts");
	crtcIdx_ = -1;
	for (int i = 0; i < drm_resources_->count_crtcs; ++i)
	{
		if (crtcId_ == drm_resources_->crtcs[i])
		{
			crtcIdx_ = i;
			break;
		}
	}
	if (crtcIdx_ == -1)
	{
		drmModeFreeResources(drm_resources_);
		throw std::runtime_error("drm: CRTC " + std::to_string(crtcId_) + " not found");
	}

	findPlane();
	
	drmModeFreeEncoder(drm_encoder_);
	drmModeFreeConnector(drm_connector_);
	drmModeFreeResources(drm_resources_);
	gbm_dev_ = gbm_create_device(drmfd_);
	gbm_surface_ = gbm_surface_create(gbm_dev_, drm_mode_.hdisplay, drm_mode_.vdisplay, GBM_FORMAT_ARGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
			
	if (!gbm_surface_)
		throw std::runtime_error("failed to create gbm surface\n");
		
	init_EGL();
	
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	//eglSwapBuffers(egl_display_, egl_surface_);
	//bo = gbm_surface_lock_front_buffer(gbm_surface_);
	//std::cout << "im here\n";
	//fb = drm_fb_get_from_bo(bo);
	//std::cout << "im here\n";

	///* set mode: */
	//if(drmModeSetCrtc(drmfd_, crtcId_, fb->fb_id, 0, 0, &conId_, 1, drm_mode_))
		//throw std::runtime_error("failed to set mode\n");
		
	//findPlane();
	
	return 0;
}

void makeBuffer(int fd, libcamera::StreamConfiguration const &info, libcamera::FrameBuffer *buffer, int camera_num)
{
	if (first_time_)
	{
	// This stuff has to be delayed until we know we're in the thread doing the display.
		if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_))
			throw std::runtime_error("eglMakeCurrent failed");
		gl_setup();
		first_time_ = false;
	}

	EGLint attribs[] = {
		EGL_WIDTH, static_cast<EGLint>(info.size.width),
		EGL_HEIGHT, static_cast<EGLint>(info.size.height),
		EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_YUV420,
		EGL_DMA_BUF_PLANE0_FD_EXT, fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLint>(info.stride),
		EGL_DMA_BUF_PLANE1_FD_EXT, fd,
		EGL_DMA_BUF_PLANE1_OFFSET_EXT, static_cast<EGLint>(info.stride * info.size.height),
		EGL_DMA_BUF_PLANE1_PITCH_EXT, static_cast<EGLint>(info.stride / 2),
		EGL_DMA_BUF_PLANE2_FD_EXT, fd,
		EGL_DMA_BUF_PLANE2_OFFSET_EXT, static_cast<EGLint>(info.stride * info.size.height + (info.stride / 2) * (info.size.height / 2)),
		EGL_DMA_BUF_PLANE2_PITCH_EXT, static_cast<EGLint>(info.stride / 2),
		EGL_YUV_COLOR_SPACE_HINT_EXT, EGL_ITU_REC601_EXT,
		EGL_SAMPLE_RANGE_HINT_EXT, EGL_YUV_NARROW_RANGE_EXT,
		EGL_NONE
	};

	EGLImage image = eglCreateImageKHR(egl_display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
	if (!image)
		throw std::runtime_error("failed to import fd " + std::to_string(fd));

	if (camera_num == 1){
		//if (glIsTexture(FramebufferName))
		//	glDeleteTextures(1, &FramebufferName);
		//glDeleteTextures(1, &FramebufferName);
		//glGenTextures(1, &FramebufferName);
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, FramebufferName);
	}else if (camera_num == 2){
		//if (glIsTexture(FramebufferName2))
		//	glDeleteTextures(1, &FramebufferName2);
		//glDeleteTextures(1, &FramebufferName2);
		//glGenTextures(1, &FramebufferName2);
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, FramebufferName2);
	}
	
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);

	eglDestroyImageKHR(egl_display_, image);
}

void displayframe(int width, int height)
{
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	
	width = width/2;
	
	//Draw camera 1
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, FramebufferName);
	glViewport(0,0,width,height);
	//glBindFramebuffer(GL_FRAMEBUFFER, 0);    // do i need this?
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	
	//Draw camera 2
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, FramebufferName2);
	glViewport(width,0,width,height);
	//glBindFramebuffer(GL_FRAMEBUFFER, 0);    // do i need this?
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	
	//eglSwapBuffers(egl_display_, egl_surface_);
	gbmSwapBuffers(&egl_display_, &egl_surface_);
}

void exit_drm(void)
{
	eglDestroyContext(egl_display_, egl_context_);
	eglDestroySurface(egl_display_, egl_surface_);
	eglTerminate(egl_display_);
	gbmClean();

	close(drmfd_);
}

