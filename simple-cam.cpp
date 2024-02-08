/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022, Peyton Howe
 *
 * A simple dual-camera libcamera capture program
 */

#include <iomanip>
#include <iostream>
#include <string> 
#include <memory>
#include <boost/lexical_cast.hpp>
#include <queue>
#include <sys/mman.h>

#include "event_loop.h"
#include "preview.h"


struct options
{
	int dual_cameras;
	unsigned int width;
	unsigned int height;
	unsigned int prev_x, prev_y, prev_width, prev_height;
	float fps;
	float shutterSpeed;
	std::string exposure;
	int exposure_index;
	int timeout;
	int buffer_count;
};

std::unique_ptr<options> options_;
int cam_exposure_index;
uint64_t last_timestamp_ = 0;
uint64_t last_timestamp2_ = 0;

using namespace libcamera;
std::unique_ptr<CameraManager> cm;
static std::shared_ptr<Camera> cameras[2];
std::unique_ptr<CameraConfiguration> configs[2];
std::vector<std::unique_ptr<Request>> requests[2];
std::map<FrameBuffer *, std::vector<libcamera::Span<uint8_t>>> mapped_buffers[2];
std::map<Stream *, std::queue<FrameBuffer *>> frame_buffers[2];
FrameBufferAllocator *allocators[2];
static EventLoop loop;

static void processRequest(Request *request);
static void processRequest2(Request *request);

static void requestComplete(Request *request)
{
	if (request->status() == Request::RequestCancelled)
		return;
	loop.callLater(std::bind(&processRequest, request));
}

static void requestComplete2(Request *request)
{
    if (request->status() == Request::RequestCancelled)
		return;
	loop.callLater(std::bind(&processRequest2, request));
}

static void processRequest(Request *request)
{
	//float framerate = 0;
	//auto ts = request->metadata().get(controls::SensorTimestamp);
	//uint64_t timestamp = ts ? *ts : request->buffers().begin()->second->metadata().timestamp;
	//if (last_timestamp_ == 0 || last_timestamp_ == timestamp)
		//framerate = 0;
	//else
		//framerate = 1e9 / (timestamp - last_timestamp_);
	//last_timestamp_ = timestamp;
	
	////if (framerate < 55)
	//std::cout << "Cam1 fps: " << framerate << '\n';
	
	const Request::BufferMap &buffers = request->buffers();
	for (auto bufferPair : buffers) {
		const Stream *stream = bufferPair.first;
		FrameBuffer *buffer = bufferPair.second;
		StreamConfiguration const &cfg = stream->configuration();
		int fd = buffer->planes()[0].fd.get();
		
		makeBuffer(fd, cfg, buffer, 1);
	}
	
	/* Re-queue the Request to the camera. */
	
	request->reuse(Request::ReuseBuffers);
	cameras[0]->queueRequest(request);
}

static void processRequest2(Request *request)
{	
	const Request::BufferMap &buffers2 = request->buffers();
	for (auto bufferPair : buffers2) {
		const Stream *stream = bufferPair.first;
		FrameBuffer *buffer2 = bufferPair.second;
		StreamConfiguration const &cfg2 = stream->configuration();
		int fd2 = buffer2->planes()[0].fd.get();
		
		makeBuffer(fd2, cfg2, buffer2, 2);
	}
	
	/* Re-queue the Request to the camera. */
	request->reuse(Request::ReuseBuffers);
	cameras[1]->queueRequest(request);
}

void makeRequests(int i)
{
	auto free_buffers(frame_buffers[i]);
	while (true)
	{
		for (StreamConfiguration &cfg : *configs[i])
		{
			Stream *stream = cfg.stream();
			if (stream == configs[i]->at(0).stream())
			{
				if (free_buffers[stream].empty())
				{

					std::cout << "Requests created\n";
					return;
				}
				std::unique_ptr<Request> request = cameras[i]->createRequest();
				if (!request)
					throw std::runtime_error("failed to make request");
				requests[i].push_back(std::move(request));
			}
			else if (free_buffers[stream].empty())
				throw std::runtime_error("concurrent streams need matching numbers of buffers");

			FrameBuffer *buffer = free_buffers[stream].front();
			free_buffers[stream].pop();
			if (requests[i].back()->addBuffer(stream, buffer) < 0)
				throw std::runtime_error("failed to add buffer to request");
		}
	}
}

void configureCamera(int i, options& params)
{
	std::string cameraId = cm->cameras()[i]->id();
	cameras[i] = cm->get(cameraId);
	cameras[i]->acquire();
	std::cout << "Acquired Camera: " << cameras[i]->id() << '\n';

	configs[i] = cameras[i]->generateConfiguration( { StreamRole::Viewfinder } );
	
	if (!configs[i])
		std::cout << "failed to generate viewfinder configuration\n";
	
    StreamConfiguration &streamConfig = configs[i]->at(0);
	std::cout << "Default viewfinder configuration is: " << streamConfig.toString() << std::endl;
	
	int val = cameras[i]->configure(configs[i].get());
    if (val) {
        std::cout << "CONFIGURATION FAILED!" << std::endl;
        std::exit(EXIT_FAILURE);
    }
	
    Size size(1280, 960);
	auto area = cameras[i]->properties().get(properties::PixelArrayActiveAreas);
	if (params.width != 0 && params.height != 0) //width and height were input
		size=Size(params.width, params.height);
    else if (area)
	{
		// The idea here is that most sensors will have a 2x2 binned mode that
		// we can pick up.
		size = (*area)[0].size() / 2;
		//size = size.boundedToAspectRatio(Size(params.width, params.height));
		size.alignDownTo(2, 2); // YUV420 will want to be even
		std::cout << "Viewfinder size chosen is " << size.toString() << std::endl;
	}
	
	configs[i]->at(0).pixelFormat = libcamera::formats::YUV420;
	configs[i]->at(0).size = size;
	
	configs[i]->at(0).bufferCount = params.buffer_count;
	
	configs[i]->validate();
	std::cout << "Validated viewfinder configuration is: "
		  << streamConfig.toString() << std::endl;
		  
	val = cameras[i]->configure(configs[i].get());
	if (val) {
		std::cout << "CONFIGURATION FAILED!" << std::endl;
		//return EXIT_FAILURE;
	}

	/*
	 * Once we have a validated configuration, we can apply it to the
	 * Camera.
	 */
	cameras[i]->configure(configs[i].get());
	
	allocators[i] = new FrameBufferAllocator(cameras[i]);
	for (StreamConfiguration &cfg : *configs[i]) {
		Stream *stream = cfg.stream();
		if (allocators[i]->allocate(stream) < 0)
			std::cerr << "Can't allocate buffers" << std::endl;
			
		for (const std::unique_ptr<FrameBuffer> &buffer : allocators[i]->buffers(stream))
		{
			size_t buffer_size = 0;
			for (unsigned j = 0; j < buffer->planes().size(); j++)
			{
				const FrameBuffer::Plane &plane = buffer->planes()[j];
				buffer_size += plane.length;
				
				if (j == buffer->planes().size() -1 || plane.fd.get() != buffer->planes()[j+1].fd.get())
				{
					void *memory = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, plane.fd.get(), 0);
					mapped_buffers[i][buffer.get()].push_back(Span<uint8_t>(static_cast<uint8_t*>(memory), buffer_size));
					buffer_size = 0;
				}
			}
			frame_buffers[i][stream].push(buffer.get());
		}
		

		size_t allocated = allocators[i]->buffers(cfg.stream()).size();
		std::cout << "Allocated " << allocated << " buffers for stream" << std::endl;
	}
	
	makeRequests(i);
}

int main(int argc, char **argv)
{
	options params = {
		.dual_cameras = 1,
		.width = 0, //default
		.height = 0, //default
		.prev_x = 0, 
		.prev_y = 0, 
		.prev_width = 1920, 
		.prev_height = 1080,
		.fps = 30.0,
		.shutterSpeed = 0,
		.exposure = "normal",
		.exposure_index = cam_exposure_index,
		.timeout = 10,
		.buffer_count = 4
	};
			
	int arg;
	while ((arg = getopt(argc, argv, "r:w:h:p:f:s:e:t:b:")) != -1)
	{
		switch (arg)
		{
			case 'd':
				params.dual_cameras = std::stoi(optarg);
				break;
			case 'w':
				params.width = std::stoi(optarg);
				break;
			case 'h':
				params.height = std::stoi(optarg);
				break;
			case 'p':
			    sscanf(optarg, "%u,%u,%u,%u", &params.prev_x, &params.prev_y, &params.prev_width, &params.prev_height);
				break;
			case 'f':
				params.fps = std::stoi(optarg);
				break;
			case 's':
				params.shutterSpeed = std::stoi(optarg);
				break;
			case 'e':
				if (strcmp(optarg, "normal") == 0) params.exposure = "normal";
				else if (strcmp(optarg, "sport") == 0) params.exposure = "sport";
				else if (strcmp(optarg, "short") == 0) params.exposure = "short";
				else if (strcmp(optarg, "long") == 0) params.exposure = "long";
				else if (strcmp(optarg, "custom") == 0) params.exposure = "custom";
				else printf("Unkown exposure mode, defaulting to noraml\n");
				break;
			case 't':
				params.timeout = std::stoi(optarg);
				break;
			case 'b':
				params.buffer_count = std::stoi(optarg);
				break;
			default:
				printf("Usage: %s [-d dual cameras] [-w width] [-h height] [-p x,y,width,height][-f fps] [-s shutter-speed-ns] [-e exposure] [-t timeout] \n", argv[0]);
				break;
		}
	}
	
	if (arg < 1)
		printf("Usage: %s [-d dual cameras] [-w width] [-h height] [-p width,height,x_off,y_off][-f fps] [-s shutter-speed-ns] [-e exposure] [-t timeout] \n", argv[0]);
	
	// Initialize the camera Manager
	cm = std::make_unique<CameraManager>();
	cm->start();

	// Ensure that cameras are connected
	if (cm->cameras().empty()) {
		std::cout << "No cameras were identified on the system."
			  << std::endl;
		cm->stop();
		return EXIT_FAILURE;
	}
	
	// Setup each camera
	for (int i = 0; i < 2; i++) {
		configureCamera(i, params);
	}
	
	cameras[0]->requestCompleted.connect(requestComplete);
	cameras[1]->requestCompleted.connect(requestComplete2);
	
	ControlList controls;
	
	std::map<std::string, int> exposure_table =
		{ { "normal", libcamera::controls::ExposureNormal },
			{ "sport", libcamera::controls::ExposureShort },
			{ "short", libcamera::controls::ExposureShort },
			{ "long", libcamera::controls::ExposureLong },
			{ "custom", libcamera::controls::ExposureCustom } };
			
	if (exposure_table.count(params.exposure) == 0)
		throw std::runtime_error("Invalid exposure mode:" + params.exposure);
	cam_exposure_index = exposure_table[params.exposure];
	
	int64_t frame_time = 1000000 / params.fps; // in us
	
	controls.set(controls::AeExposureMode, cam_exposure_index);
	controls.set(controls::ExposureTime, params.shutterSpeed);
	controls.set(controls::FrameDurationLimits, libcamera::Span<const int64_t, 2>({ frame_time, frame_time }));
	
	//if (!controls.get(controls::Brightness)) // Adjust the brightness of the output images, in the range -1.0 to 1.0
	//	controls.set(controls::Brightness, 0.0);
	//if (!controls.get(controls::Contrast)) // Adjust the contrast of the output image, where 1.0 = normal contrast
	//	controls.set(controls::Contrast, 1.0);
    
    // Set the exposure time
    //controls.set(controls::ExposureTime, frame_time);
    
    for (int i = 0; i < 2; i++) {
		cameras[i]->start(&controls);
		for (std::unique_ptr<Request> &request : requests[i])
			cameras[i]->queueRequest(request.get());
	}
		
	// Setup EGL context
	makeWindow("simple-cam", params.prev_x, params.prev_y, params.prev_width, params.prev_height);

	loop.timeout(params.timeout);
	int ret = loop.exec(params.prev_width, params.prev_height, params.timeout);
	std::cout << "Capture ran for " << params.timeout << " seconds and "
		  << "stopped with exit status: " << ret << std::endl;


	for (int i = 0; i < 2; i++) {
		cameras[i]->stop();
		delete allocators[i];
		cameras[i]->release();
		cameras[i].reset();
		requests[i].clear();
	}
    cm->stop();
	cleanup();

	return EXIT_SUCCESS;
}
