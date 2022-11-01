/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020, Ideas on Board Oy.
 *
 * A simple libcamera capture example
 */

#include <iomanip>
#include <iostream>
#include <string> 
#include <memory>
#include <boost/lexical_cast.hpp>

//#include <libcamera/libcamera.h>

#include "event_loop.h"
#include "eglUtil.h"

#define TIMEOUT_SEC 10

using namespace libcamera;
static std::shared_ptr<Camera> camera;
static std::shared_ptr<Camera> camera2;
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
	std::cout << std::endl
		  << "Request completed: " << request->toString() << std::endl;
		  
	const ControlList &requestMetadata = request->metadata();
	for (const auto &ctrl : requestMetadata) {
		const ControlId *id = controls::controls.at(ctrl.first);
		const ControlValue &value = ctrl.second;

		std::cout << "\t" << id->name() << " = " << value.toString()
			  << std::endl;
	}

	const Request::BufferMap &buffers = request->buffers();
	for (auto bufferPair : buffers) {
		const Stream *stream = bufferPair.first;
		FrameBuffer *buffer = bufferPair.second;
		const FrameMetadata &metadata = buffer->metadata();
		
		StreamConfiguration const &cfg = stream->configuration();
		int fd = buffer->planes()[0].fd.get();
		 
		//size_t size = buffer->metadata().planes[0].bytesused;
		//const FrameBuffer::Plane &plane = buffer->planes().front();
		//void *memory = mmap(NULL, plane.length, PROT_READ, MAP_SHARED, plane.fd.fd(), 0);
		
		//std::cout << "width: " << cfg.size.width << " height: " << cfg.size.height << " stride: " << cfg.stride << "\n";
		//std::cout << "fd: " << fd << "\n";

		/* Print some information about the buffer which has completed. */
		std::cout << " seq: " << std::setw(6) << std::setfill('0') << metadata.sequence
			  << " timestamp: " << metadata.timestamp
			  << " bytesused: ";


		//std::cout << "Stride: " << stream->stride << " Width: " << stream->width << " Height: " << stream->height << std::endl;
		unsigned int nplane = 0;
		for (const FrameMetadata::Plane &plane : metadata.planes())
		{
			std::cout << plane.bytesused;
			if (++nplane < metadata.planes().size())
				std::cout << "/";
		}

		std::cout << std::endl;
		
		makeBuffer(fd, cfg, buffer, 1);

		/*
		 * Image data can be accessed here, but the FrameBuffer
		 * must be mapped by the application
		 */
	}

	/* Re-queue the Request to the camera. */	
	request->reuse(Request::ReuseBuffers);
	camera->queueRequest(request);
}

static void processRequest2(Request *request)
{
	std::cout << std::endl
		  << "Request2 completed: " << request->toString() << std::endl;

	const ControlList &requestMetadata = request->metadata();
	for (const auto &ctrl : requestMetadata) {
		const ControlId *id = controls::controls.at(ctrl.first);
		const ControlValue &value = ctrl.second;

		std::cout << "\t" << id->name() << " = " << value.toString()
			  << std::endl;
	}
	
	const Request::BufferMap &buffers2 = request->buffers();
	for (auto bufferPair : buffers2) {
		//const Stream *stream = bufferPair.first;
		FrameBuffer *buffer2 = bufferPair.second;
		const FrameMetadata &metadata = buffer2->metadata();
		
		//StreamConfiguration const &cfg2 = stream->configuration();
		//int fd2 = buffer2->planes()[0].fd.get();

		/* Print some information about the buffer which has completed. */
		std::cout << " seq: " << std::setw(6) << std::setfill('0') << metadata.sequence
			  << " timestamp: " << metadata.timestamp
			  << " bytesused: ";

		unsigned int nplane = 0;
		for (const FrameMetadata::Plane &plane : metadata.planes())
		{
			std::cout << plane.bytesused;
			if (++nplane < metadata.planes().size())
				std::cout << "/";
		}

		std::cout << std::endl;
		
		//makeBuffer(fd2, cfg2, buffer2, 2);

		/*
		 * Image data can be accessed here, but the FrameBuffer
		 * must be mapped by the application
		 */
	}

	/* Re-queue the Request to the camera. */
	request->reuse(Request::ReuseBuffers);
	camera2->queueRequest(request);
}

std::string cameraName(Camera *camera)
{

	//const ControlList &props = camera->properties();
	std::string name;
	auto location = camera->properties().get(properties::Location);
	
	if (location == properties::CameraLocationFront)
		name = "Internal front camera";
	else if (location == properties::CameraLocationBack)
		name = "Internal back camera";
	else if (location == properties::CameraLocationExternal) {
		name = "External camera";
		//auto model = camera->properties().get(properties::Model);
		//name = " '" + model + "'";
	}

	name += " (" + camera->id() + ")";

	return name;
}

int main()
{
	std::unique_ptr<CameraManager> cm = std::make_unique<CameraManager>();
	cm->start();

	/*
	 * Just as a test, generate names of the Cameras registered in the
	 * system, and list them.
	 */
	for (auto const &camera : cm->cameras())
		std::cout << " - " << cameraName(camera.get()) << std::endl;

	if (cm->cameras().empty()) {
		std::cout << "No cameras were identified on the system."
			  << std::endl;
		cm->stop();
		return EXIT_FAILURE;
	}

	std::string cameraId = cm->cameras()[0]->id();
	camera = cm->get(cameraId);
	camera->acquire();
	
	std::string cameraId2 = cm->cameras()[1]->id();
	camera2 = cm->get(cameraId2);
	camera2->acquire();

	std::unique_ptr<CameraConfiguration> config =
		camera->generateConfiguration( { StreamRole::Viewfinder } );
		
	std::unique_ptr<CameraConfiguration> config2 =
		camera2->generateConfiguration( { StreamRole::Viewfinder } );
		
	if (!config)
		printf("failed to generate viewfinder configuration");
	if (!config2)
		printf("failed to generate viewfinder configuration for camera 2");
	
	StreamConfiguration &streamConfig = config->at(0);
	std::cout << "Default viewfinder configuration is: "
		  << streamConfig.toString() << std::endl;
		  
	StreamConfiguration &streamConfig2 = config2->at(0);
	std::cout << "Default viewfinder configuration for camera 2 is: "
		  << streamConfig2.toString() << std::endl;
		  
    Size size(1280, 960);
	auto area = camera->properties().get(properties::PixelArrayActiveAreas);
	
    if (area)
	{
		// The idea here is that most sensors will have a 2x2 binned mode that
		// we can pick up. If it doesn't, well, you can always specify the size
		// you want exactly with the viewfinder_width/height options_->
		size = (*area)[0].size() / 2;
		// If width and height were given, we might be switching to capture
		// afterwards - so try to match the field of view.
		//if (options_->width && options_->height)
		//	size = size.boundedToAspectRatio(Size(options_->width, options_->height));
		size.alignDownTo(2, 2); // YUV420 will want to be even
		std::cout << "Viewfinder size chosen is " << size.toString() << std::endl;
	}

	// Finally trim the image size to the largest that the preview can handle.
	Size max_size;
	//preview_->MaxImageSize(max_size.width, max_size.height);
	if (max_size.width && max_size.height)
	{
		size.boundTo(max_size.boundedToAspectRatio(size)).alignDownTo(2, 2);
		std::cout << "Final viewfinder size is " << size.toString() << std::endl;
	}
	
    config->at(0).pixelFormat = libcamera::formats::YUV420;
	config->at(0).size = size;
	
	config2->at(0).pixelFormat = libcamera::formats::YUV420;
	config2->at(0).size = size;

	/*
	 * The Camera configuration procedure fails with invalid parameters.
	 */
#if 0
	streamConfig.size.width = 0; //4096
	streamConfig.size.height = 0; //2560
	
	streamConfig2.size.width = 0; //4096
	streamConfig2.size.height = 0; //2560

	int ret = camera->configure(config.get());
	if (ret) {
		std::cout << "CONFIGURATION FAILED!" << std::endl;
		return EXIT_FAILURE;
	}
	
	int ret2 = camera2->configure(config2.get());
	if (ret2) {
		std::cout << "CONFIGURATION FAILED!" << std::endl;
		return EXIT_FAILURE;
	}
#endif

	config->validate();
	std::cout << "Validated viewfinder configuration is: "
		  << streamConfig.toString() << std::endl;
		  
	config2->validate();
	std::cout << "Validated viewfinder configuration for camera 2 is: "
		  << streamConfig2.toString() << std::endl;

	/*
	 * Once we have a validated configuration, we can apply it to the
	 * Camera.
	 */
	camera->configure(config.get());
	camera2->configure(config2.get());

	FrameBufferAllocator *allocator = new FrameBufferAllocator(camera);

	for (StreamConfiguration &cfg : *config) {
		int ret = allocator->allocate(cfg.stream());
		if (ret < 0) {
			std::cerr << "Can't allocate buffers" << std::endl;
			return EXIT_FAILURE;
		}

		size_t allocated = allocator->buffers(cfg.stream()).size();
		std::cout << "Allocated " << allocated << " buffers for stream" << std::endl;
	}
	
	FrameBufferAllocator *allocator2 = new FrameBufferAllocator(camera2);

	for (StreamConfiguration &cfg2 : *config2) {
		int ret = allocator2->allocate(cfg2.stream());
		if (ret < 0) {
			std::cerr << "Can't allocate buffers for stream 2" << std::endl;
			return EXIT_FAILURE;
		}

		size_t allocated = allocator2->buffers(cfg2.stream()).size();
		std::cout << "Allocated " << allocated << " buffers for stream 2" << std::endl;
	}
	Stream *stream = streamConfig.stream();
	const std::vector<std::unique_ptr<FrameBuffer>> &buffers = allocator->buffers(stream);
	std::vector<std::unique_ptr<Request>> requests;
	for (unsigned int i = 0; i < buffers.size(); ++i) {
		std::unique_ptr<Request> request = camera->createRequest();
		if (!request)
		{
			std::cerr << "Can't create request" << std::endl;
			return EXIT_FAILURE;
		}

		const std::unique_ptr<FrameBuffer> &buffer = buffers[i];
		int ret = request->addBuffer(stream, buffer.get());
		if (ret < 0)
		{
			std::cerr << "Can't set buffer for request"
				  << std::endl;
			return EXIT_FAILURE;
		}

		/*
		 * Controls can be added to a request on a per frame basis.
		 */
		//ControlList &controls = request->controls();
		//controls.set(controls::Brightness, 0.5);

		requests.push_back(std::move(request));
	}
	
	Stream *stream2 = streamConfig2.stream();
	const std::vector<std::unique_ptr<FrameBuffer>> &buffers2 = allocator2->buffers(stream2);
	std::vector<std::unique_ptr<Request>> requests2;
	for (unsigned int i = 0; i < buffers2.size(); ++i) {
		std::unique_ptr<Request> request2 = camera2->createRequest();
		if (!request2)
		{
			std::cerr << "Can't create request for camera 2" << std::endl;
			return EXIT_FAILURE;
		}

		const std::unique_ptr<FrameBuffer> &buffer2 = buffers2[i];
		int ret2 = request2->addBuffer(stream2, buffer2.get());
		if (ret2 < 0)
		{
			std::cerr << "Can't set buffer for request for camera 2"
				  << std::endl;
			return EXIT_FAILURE;
		}

		/*
		 * Controls can be added to a request on a per frame basis.
		 */
		//ControlList &controls2 = request2->controls();
		//controls2.set(controls::Brightness, 0.5);

		requests2.push_back(std::move(request2));
	}

	camera->requestCompleted.connect(requestComplete);
	camera2->requestCompleted.connect(requestComplete2);
	
    ControlList controls;
    int64_t frame_time = 1000000 / 30;
    // Set frame rate
	controls.set(controls::FrameDurationLimits, { frame_time, frame_time });
    // Adjust the brightness of the output images, in the range -1.0 to 1.0
    controls.set(controls::Brightness, 0.0);
    // Adjust the contrast of the output image, where 1.0 = normal contrast
    controls.set(controls::Contrast, 1.0);
    // Set the exposure time
    controls.set(controls::ExposureTime, frame_time);
    

	camera->start(&controls);
	camera2->start(&controls);
	
	for (std::unique_ptr<Request> &request : requests)
		camera->queueRequest(request.get());
		

	for (std::unique_ptr<Request> &request2 : requests2)
		camera2->queueRequest(request2.get());
		
	// Setup EGL context
	setupEGL("simple-cam", 1920, 1080);
	//glClearColor(0.8f, 0.2f, 0.1f, 1.0f);

	loop.timeout(TIMEOUT_SEC);
	int ret = loop.exec();
	std::cout << "Capture ran for " << TIMEOUT_SEC << " seconds and "
		  << "stopped with exit status: " << ret << std::endl;

	camera->stop();
	camera2->stop();
	allocator->free(stream);
	allocator2->free(stream2);
	delete allocator;
	delete allocator2;
	camera->release();
	camera2->release();
	camera.reset();
	camera2.reset();
	cm->stop();

	return EXIT_SUCCESS;
}
