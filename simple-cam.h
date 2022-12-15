#pragma once

struct options
{
	std::string render_mode;
	unsigned int width;
	unsigned int height;
	std::string preview;
	unsigned int  prev_x, prev_y, prev_width, prev_height;
	float fps;
	float shutterSpeed;
	std::string exposure;
	int exposure_index;
};

std::unique_ptr<options> options_;

std::string egl_render = "EGL";
std::string drm_render = "DRM";
unsigned int cam_width = 1920;
unsigned int cam_height = 1080;
std::string preview_mode = "0,0,1920,1080";
float cam_fps = 30.0;
float cam_shutter = 0;
std::string cam_exposure = "normal";
std::string cam_exposure1 = "short";
std::string cam_exposure2 = "long";
std::string cam_exposure3 = "custom";
int cam_exposure_index;
