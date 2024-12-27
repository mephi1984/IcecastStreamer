// Conversion528.cpp : Defines the entry point for the console application.
//

#ifdef _WIN32
#include <conio.h>

#include "stdafx.h"
#endif
#include "wave/WaveFile.h"

#include <memory.h>

#define _WRITE_ERROR_DESCR_
#ifdef _WRITE_ERROR_DESCR_

#include <fstream>
#endif

#include "boost/filesystem.hpp"
#include <boost/thread/thread.hpp>
#include "boost/asio.hpp"


#include <icecast/IcecastStreamer.h>

#include <random>
#include <algorithm>
#include <iterator>
#include <iostream>


boost::asio::io_service ioService;
boost::asio::io_service::work work(ioService);
boost::thread_group threadPool;

//IcecastStreamer streamer{ ioService, "vm493.vmware.nano.lv", "80" };
#ifdef _WIN32
IcecastStreamer streamer{ ioService, "127.0.0.1", "8000" };

#else
//IcecastStreamer streamer{ ioService, "528records.com", "8000" };
IcecastStreamer streamer{ ioService, "127.0.0.1", "8000" };
#endif
//IcecastStreamer streamer{ ioService, "528records.com", "8000" };
//IcecastStreamer streamer{ ioService, "127.0.0.1", "80" };

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


void streamPlaylist(const std::vector<std::string>& playlist)
{
	auto promise = std::make_shared<std::promise<void>>();

	ContentToStream contentToStream(playlist);

	ioService.post([contentToStream, promise]()
	{
		std::cout << "StreamPlaylist inner 1" << std::endl;
		//streamer.streamFile(contentToStream, promise);

		streamer.streamFileLooped(contentToStream, promise);
	});

	promise->get_future().wait();
}




int recordCallback(const void* inputBuffer, void* outputBuffer,
	unsigned long framesPerBuffer,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void* userData) {
	AudioDataList* dataList = static_cast<AudioDataList*>(userData);
	const short* input = static_cast<const short*>(inputBuffer);

	if (inputBuffer == nullptr) return paContinue;

	dataList->m.lock();

	if (dataList->audioData.size() == 0)
	{
		dataList->audioData.emplace_back();
	}

	AudioData& data = dataList->audioData.back();

	size_t samplesReady = framesPerBuffer * NUM_CHANNELS;

	if (data.currentSample + samplesReady >= MAX_SAMPLES) {
		size_t samplesToCopy = MAX_SAMPLES - data.currentSample;
		std::copy(input, input + samplesToCopy, data.buffer + data.currentSample);

		size_t leftover = samplesReady - samplesToCopy;

		//Mark data block as ready:
		data.currentSample = 0;
		data.ready = true;

		dataList->audioData.emplace_back();

		// Warning! Here might be overflow
		if (leftover > MAX_SAMPLES)
		{
			throw std::runtime_error("Too much!");
		}

		if (leftover > 0)
		{
			AudioData& newData = dataList->audioData.back();

			std::copy(input + samplesToCopy, input + samplesReady, newData.buffer);
			newData.currentSample = leftover;
		}
	}
	else
	{
		std::copy(input, input + samplesReady, data.buffer + data.currentSample);
		data.currentSample += samplesReady;
	}
	dataList->m.unlock();
	return paContinue;
}

void streamAudio()
{
	auto promise = std::make_shared<std::promise<void>>();

	std::shared_ptr<AudioDataList> audioDataList = std::make_shared<AudioDataList>();

	Pa_Initialize();

	
	//Regular recording
	/*
	PaStream* stream;
	Pa_OpenDefaultStream(&stream, NUM_CHANNELS, 0, paInt16, 48000,
		FRAMES_PER_BUFFER, recordCallback, audioDataList.get());
	Pa_StartStream(stream);
*/	

	
	int numDevices = Pa_GetDeviceCount();
	if (numDevices < 0) {
		std::cerr << "Ошибка при получении списка устройств: " << Pa_GetErrorText(numDevices) << std::endl;
		return;
	}

	std::cout << "Список доступных аудиоустройств:\n";
	for (int i = 0; i < numDevices; ++i) {
		const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
		std::cout << "Device " << i << ": " << deviceInfo->name << "\n";
	}
	
	
	//Loopback
	
	PaDeviceIndex deviceIndex = Pa_GetHostApiInfo(PaHostApiTypeId::paWASAPI)->defaultOutputDevice;
	//PaDeviceIndex deviceIndex = 3;
	PaStreamParameters inputParameters;
	inputParameters.device = deviceIndex;
	inputParameters.channelCount = NUM_CHANNELS;
	inputParameters.sampleFormat = paInt16;
	inputParameters.suggestedLatency = Pa_GetDeviceInfo(deviceIndex)->defaultLowInputLatency;
	inputParameters.hostApiSpecificStreamInfo = nullptr;
	
	PaStream* stream;
	Pa_OpenStream(&stream, &inputParameters, nullptr, SAMPLE_RATE, FRAMES_PER_BUFFER, paClipOff, recordCallback, audioDataList.get());
	Pa_StartStream(stream);
	
	std::cout << "Recording for 1 seconds..." << std::endl;
	Pa_Sleep(1000);

	ioService.post([audioDataList, promise]()
		{
			std::cout << "StreamPlaylist inner 1" << std::endl;
			//streamer.streamFile(contentToStream, promise);

			streamer.streamVoice(audioDataList, promise);
		});


	std::cout << "Started streaming, continue recording..." << std::endl;

	std::cout << "Press any key to top:" << std::endl;
	_getwch();

	Pa_StopStream(stream);
	Pa_CloseStream(stream);
	Pa_Terminate();

	promise->get_future().wait();
}

const double CROSSFADE_STEP = 1.0;

struct CustomCrossFader : public CrossFadeSelector
{
	volatile bool doSwitchToVoice = false;
	double crossFadeTimer = 0.0; //0 - audio, 1 - voice

	void switchToVoice()
	{
		doSwitchToVoice = true;
	}

	void switchToAudio()
	{
		doSwitchToVoice = false;
	}

	virtual double audioDataVolume() override
	{
		return (1.0 - crossFadeTimer) * 0.08 + 0.02;

	}
	virtual double voiceVolume() override
	{
		return crossFadeTimer * 20.0;
	}

	virtual void update() override
	{
		if (doSwitchToVoice)
		{
			crossFadeTimer += CROSSFADE_STEP;
			if (crossFadeTimer >= 1.0)
			{
				crossFadeTimer = 1.0;
			}
		}
		else
		{
			crossFadeTimer -= CROSSFADE_STEP;
			if (crossFadeTimer <= 0.0)
			{
				crossFadeTimer = 0.0;
			}
		}
	}
};



void streamAudioAndPlaylist()
{
	auto promise = std::make_shared<std::promise<void>>();

	std::shared_ptr<AudioDataList> audioDataList = std::make_shared<AudioDataList>();

	Pa_Initialize();


	//Regular recording
	/*
	PaStream* stream;
	Pa_OpenDefaultStream(&stream, NUM_CHANNELS, 0, paInt16, 48000,
		FRAMES_PER_BUFFER, recordCallback, audioDataList.get());
	Pa_StartStream(stream);
	*/

	
	int numDevices = Pa_GetDeviceCount();
	if (numDevices < 0) {
		std::cerr << "Ошибка при получении списка устройств: " << Pa_GetErrorText(numDevices) << std::endl;
		return;
	}

	std::cout << "Список доступных аудиоустройств:\n";
	for (int i = 0; i < numDevices; ++i) {
		const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
		std::cout << "Device " << i << ": " << deviceInfo->name << " " << deviceInfo->defaultSampleRate << "\n";
	}

	
	//Loopback

	//PaDeviceIndex deviceIndex = Pa_GetHostApiInfo(PaHostApiTypeId::paWASAPI)->defaultOutputDevice;
	PaDeviceIndex deviceIndex = 18;
	PaStreamParameters inputParameters;
	inputParameters.device = deviceIndex;
	inputParameters.channelCount = NUM_CHANNELS;
	inputParameters.sampleFormat = paInt16;
	inputParameters.suggestedLatency = Pa_GetDeviceInfo(deviceIndex)->defaultLowInputLatency;
	inputParameters.hostApiSpecificStreamInfo = nullptr;

	PaStream* stream;
	Pa_OpenStream(&stream, &inputParameters, nullptr, 48000, FRAMES_PER_BUFFER, paClipOff, recordCallback, audioDataList.get());
	Pa_StartStream(stream);
	
	std::cout << "Recording for 5.0 seconds..." << std::endl;
	Pa_Sleep(5000);

	CustomCrossFader c;
	ContentToStreamWithAudio content{c, { "C:/music/ETM_Night_Run (MIX) - 02.mp3", "C:/music/ETM_Night_Run (MIX) - 02.mp3" } };
	
	content.audioData = audioDataList;


	ioService.post([content, promise]()
		{
			std::cout << "StreamPlaylist inner 1" << std::endl;


			streamer.streamComplexLooped(content, promise);
			//streamer.streamFile(contentToStream, promise);

			//streamer.streamVoice(audioDataList, promise);
		});


	std::cout << "Started streaming, continue recording. Press 0 to stop..." << std::endl;
	wchar_t ch = 0;

	while (ch != L'0')
	{
		ch = _getwch(); // Чтение символа с клавиатуры

		if (ch == L'1') {
			std::cout << "Switch to voice" << std::endl;
			c.switchToVoice();
		}
		else if (ch == L'2') {
			std::cout << "Switch to audio" << std::endl;
			c.switchToAudio();
		}
		else {
			
		}

	}

	//std::cout << "Press any key to top:" << std::endl;
	//_getwch();

	Pa_StopStream(stream);
	Pa_CloseStream(stream);
	Pa_Terminate();

	promise->get_future().wait();
}

/*int main(int argc, char* argv[])
{
#ifdef _WIN32
	std::cout << "Press any key to start playing:" << std::endl;
	_getwch();
#endif



	return 0;
}
*/

int main(int argc, char* argv[])
{
	//setlocale(LC_ALL, "Russian");
	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);


	auto f = []()
	{
		ioService.run();
	};

	threadPool.create_thread(f);


	//std::vector<std::string> listOfFiles = { "168446101.aac", "b5c751d94e8b[1].mp3" };

	//std::vector<std::string> listOfFiles = { "b5c751d94e8b[1].mp3" };

	//std::vector<std::string> listOfFiles = { "everlasting_summer_03.mp3", "bala.mp3" };

	//std::vector<std::string> listOfFiles = { "FLASHBACK FM - GTA 3.wav" };

	//std::vector<std::string> listOfFiles = { "GTA Vice City - Flash FM.mp3" };

	//std::vector<std::string> listOfFiles = { "E:/music/BAAM.wav", "E:/music/bala.wav"};
	//std::vector<std::string> listOfFiles = { "E:/music/BAAM.wav" };

	//std::vector<std::string> listOfFiles = { "E:/music/death note.ogg" };

	//std::vector<std::string> listOfFiles = { "E:/music/GTA Vice City - Flash FM.mp3" };
	//std::vector<std::string> listOfFiles = { "E:/music/Guano Apes - Open Your Eyes.mp3" };

	//std::vector<std::string> listOfFiles = { "E:/music/168446101_2channels_identical~1.aac" };

	//std::vector<std::string> listOfFiles = { "E:/music/168446101.aac" };


#ifndef _WIN32
	std::vector<std::string> listOfFiles = {
		"/home/mephi1984/icecastStreamerWork/music/168446101.aac",
		"/home/mephi1984/icecastStreamerWork/music/BAAM.wav",
		"/home/mephi1984/icecastStreamerWork/music/Guano Apes - Open Your Eyes.mp3",
		"/home/mephi1984/icecastStreamerWork/music/death note.ogg"
	};
#else
	std::vector<std::string> listOfFiles = { 
		//"E:/music/168446101.aac",
		//"E:/music/bala.wav",
		//"C:/music/ETM_Night_Run (MIX) - 02.mp3"//,
		"C:\\Work\\Projects\\VoiceWriter\\recording.wav",
		"C:\\Work\\Projects\\VoiceWriter\\recording.wav"
		//"E:/music/death note.ogg"
	};
#endif
	// !!! Pass a check that all files has extensions !!!

		
	
	std::cout << "Streamed created" << std::endl;

#ifdef _WIN32
	std::cout << "Press any key to start playing:" << std::endl;
	_getwch();
#endif

	do
	{
		//streamPlaylist(listOfFiles);

		//streamAudio();
		streamAudioAndPlaylist();


	} while (true);
	
	ioService.stop();
	threadPool.join_all();

	return 0;
}


