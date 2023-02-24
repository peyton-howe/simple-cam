/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * event_loop.cpp - Event loop based on cam
 */

#include "event_loop.h"
#include "preview.h"

#include <assert.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <iostream>
#include <algorithm>

EventLoop *EventLoop::instance_ = nullptr;

EventLoop::EventLoop()
{
	assert(!instance_); 

	evthread_use_pthreads();
	event_ = event_base_new();
	instance_ = this;
}

EventLoop::~EventLoop()
{
	instance_ = nullptr;

	event_base_free(event_);
	libevent_global_shutdown();
}

int EventLoop::exec(int width, int height, int timeout)
{
	exitCode_ = -1;
	exit_.store(false, std::memory_order_release);
	auto start_time = std::chrono::high_resolution_clock::now();
	auto lastTime = start_time;
	int nFrames = 0;
	int lastFrames = 0;
	int droppedFrames = 0;
	//int framepackage = 0;

	//while (!exit_.load(std::memory_order_acquire)) {
	while(1) {
		auto now = std::chrono::high_resolution_clock::now();
		if (timeout > 0 && now - start_time > std::chrono::milliseconds(timeout*1000))
			break;
			
		//std::cout << calls_.size() << '\n';
		if ((calls_.size() % 2 == 0) && (calls_.size() > 1)) {
			if (calls_.size() > 2){
				droppedFrames++;
				//if(calls_.size() == 4){
					////droppedFrames++;
					
					//auto middle = std::next(calls_.begin(), calls_.size() / 2);
					//std::list<std::function<void()>> left(calls_.begin(), middle), right(middle, calls_.end());
					//std::cout << "left\n";
					//calls_ = left;
					//dispatchCalls();
					//displayFrame(width, height);
					//nFrames++;
					//std::cout << "right\n";
					//calls_ = right;
					//dispatchCalls();
					//displayFrame(width, height);
				//}
				//else
					//droppedFrames++;
			}
			
			if (calls_.size() == 1)
				std::cout << "how did we get here\n";
			
			//std::cout << "calls size: " << calls_.size() << '\n';
				
			//std::cout << "calls_ size: " << calls_.size() << '\n';
			
			//std::cout << "displaying frames \n";
			//std::cout << "here doing stuff \n";
			dispatchCalls();
			//event_base_loop(event_, EVLOOP_NO_EXIT_ON_EMPTY);
			displayFrame(width, height);
			nFrames++;
			if (nFrames % 160 == 0)
			{ // Log FPS
				auto currentTime = std::chrono::high_resolution_clock::now();
				int elapsedMS = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();
				float elapsedS = (float)elapsedMS / 1000;
				lastTime = currentTime;
				int frames = (nFrames - lastFrames);
				lastFrames = nFrames;
				float fps = frames / elapsedS;
				printf("%d frames over %.2fs (%.1ffps)! \n", frames, elapsedS, fps);
				printf("%d dropped frames over %.2fs! \n", droppedFrames, elapsedS);
				droppedFrames = 0;
			}
		}
	}

	return exitCode_;
}

void EventLoop::exit(int code)
{
	exitCode_ = code;
	exit_.store(true, std::memory_order_release);
	interrupt();
}

void EventLoop::interrupt()
{
	event_base_loopbreak(event_);
}


void EventLoop::timeoutTriggered(int fd, short event, void *arg)
{
	EventLoop *self = static_cast<EventLoop *>(arg);
	self->exit();
}

void EventLoop::timeout(unsigned int sec)
{
	struct event *ev;
	struct timeval tv;

	tv.tv_sec = sec;
	tv.tv_usec = 0;
	ev = evtimer_new(event_, &timeoutTriggered, this);
	evtimer_add(ev, &tv);
}

void EventLoop::callLater(const std::function<void()> &func)
{
	{
		std::unique_lock<std::mutex> locker(lock_);
		calls_.push_back(func);
	}

	interrupt();
}

void EventLoop::dispatchCalls()
{
	std::unique_lock<std::mutex> locker(lock_);
	for (auto iter = calls_.begin(); iter != calls_.end(); ) {
		std::function<void()> call = std::move(*iter);
		iter = calls_.erase(iter);

		locker.unlock();
		call();
		locker.lock();
	}
}
