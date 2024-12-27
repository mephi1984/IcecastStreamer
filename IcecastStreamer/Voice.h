#pragma once

#include <iostream>
#include <fstream>
#include <portaudio.h>
#include <mutex>
#include <list>
#include <memory>
#include "AudioFile.h"

const int SAMPLE_RATE = 44100;
//const int SAMPLE_RATE = 48000;
const int FRAMES_PER_BUFFER = 256;
const int NUM_CHANNELS = 2;
const int SAMPLE_SIZE = 2;  // 16-bit samples


//static const int MAX_SAMPLES = SAMPLE_RATE * NUM_CHANNELS * 5; //5 sec
static const int MAX_SAMPLES = SAMPLE_RATE * NUM_CHANNELS; //1 sec

struct AudioData {
    short buffer[MAX_SAMPLES];
    size_t currentSample = 0;
	bool ready = false;
};

struct AudioDataList
{
    std::mutex m;
    std::list<AudioData> audioData;
};

class VoiceDecoder : public AudioDecoderInterface
{
public:
	std::shared_ptr<AudioDataList> audioData;

	VoiceDecoder(std::shared_ptr<AudioDataList> inAudioData)
		: audioData(inAudioData)
	{

	}

	virtual int readDuration(char* Buffer, size_t Count, std::chrono::milliseconds duration, std::chrono::milliseconds& actualDurationRead) override;

	virtual bool open(const char* fileName) override
	{
		return true;
	}
};


class VoiceDecoderConvertFrom48000 : public AudioDecoderInterface
{
public:
	std::shared_ptr<AudioDataList> audioData;

	VoiceDecoderConvertFrom48000(std::shared_ptr<AudioDataList> inAudioData)
		: audioData(inAudioData)
	{

	}

	virtual int readDuration(char* Buffer, size_t Count, std::chrono::milliseconds duration, std::chrono::milliseconds& actualDurationRead) override;

	virtual bool open(const char* fileName) override
	{
		return true;
	}
};