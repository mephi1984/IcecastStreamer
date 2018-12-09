// Conversion528.cpp : Defines the entry point for the console application.
//

#include <conio.h>
#include "stdafx.h"
#include "WaveFile.h"
#include "Conversion.h"
#include <memory.h>
#include "WaveFileAdapter.h"

#include "Converter528.h"
#include "FlatConverter.h"
#include "ExperimentalConverter.h"

#define _WRITE_ERROR_DESCR_
#ifdef _WRITE_ERROR_DESCR_

#include <fstream>
#include "ConsoleHelpers.h"

#endif

#include "boost/filesystem.hpp"
#include <boost/thread/thread.hpp>
#include "boost/asio.hpp"

#include <server/IcecastStreamer.h>

boost::asio::io_service ioService;
boost::asio::io_service::work work(ioService);
boost::thread_group serverThreadPool;

IcecastStreamer streamer{ ioService, "127.0.0.1", "8000" };

std::vector<std::string> getFileNamesInFolder(const std::string& folder)
{
	using namespace std;
	using namespace boost::filesystem;

	path p(folder);

	std::vector<std::string> result;

	directory_iterator end_itr;

	// cycle through the directory
	for (directory_iterator itr(p); itr != end_itr; ++itr)
	{
		// If it's not a directory, list it. If you want to list directories too, just remove this check.
		if (is_regular_file(itr->path())) {
			// assign current file name to current_file and echo it out to the console.
			string current_file = itr->path().string();
			
			result.push_back(current_file);
		}
	}

	return result;
}

void streamFile(const std::string& fileName)
{
	std::function<void()> func{ [fileName]() { streamer.streamFile(fileName); } };

	auto task = std::make_shared<std::packaged_task<void()>>(func);
	//auto fut = task->get_future();

	ioService.post([task]()
	{
		(*task)();
	});

	//fut.wait();
}

void StreamFile()
{
	std::cout << "Choose a file:" << std::endl;
	std::cout << "1) test/original.wav" << std::endl;

	char key = _getch();
	_getch();
	switch (key)
	{
	case '1': streamFile("test/original.wav"); break;
	}
}

void WrapFile()
{
	std::cout << "Choose a file:" << std::endl;
	std::cout << "1) test/streamed.wav -> test/wrapped.wav" << std::endl;

	char key = _getch();
	_getch();
	switch (key)
	{
	case '1': streamer.saveWaveSound("test/streamed.wav", "test/wrapped.wav"); break;
	}
}

int main(int argc, char* argv[])
{
	auto f = []()
	{
		ioService.run();
	};

	serverThreadPool.create_thread(f);

	char key;
	do
	{
		std::cout << "Choose an action:" << std::endl;
		std::cout << "1) stream sound from a file" << std::endl;
		std::cout << "2) wrap downloaded streamed sound into a file" << std::endl;

		key = _getch();
		_getch();

		switch (key)
		{
		case '1':
			StreamFile();
			break;

		case '2':
			WrapFile();
			break;
		}

	} while (true);

	ioService.stop();
	serverThreadPool.join_all();

	return 0;
}

