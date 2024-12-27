#include "Voice.h"
#include <vector>
#include <samplerate.h>

int VoiceDecoder::readDuration(char* Buffer, size_t Count, std::chrono::milliseconds duration, std::chrono::milliseconds& actualDurationRead)
{

	int initialReadCount = (NUM_CHANNELS * SAMPLE_RATE * SAMPLE_SIZE * duration.count() / 1000);
	int readCount = initialReadCount;

	if (readCount % 2 == 1)
	{
		throw std::runtime_error("Sample must be divisible by 2!");
	}

	//int initialReadCountInShort = initialReadCount / 2;
	//int readCountInShort = readCount / 2;

	int actualRead = 0;

	audioData->m.lock();

	while (readCount > 0)
	{

		if (audioData->audioData.size() == 0)
		{
			// If we are here, there is no audio data left. Return what we had processed
			audioData->m.unlock();
			actualDurationRead = std::chrono::milliseconds(duration.count() * actualRead / initialReadCount);

			return actualRead;
		}

		AudioData& data = audioData->audioData.front();

		if (data.ready == false)
		{
			// If we are here, the next block is not ready yet. Return what we had processed
			audioData->m.unlock();
			actualDurationRead = std::chrono::milliseconds(duration.count() * actualRead / initialReadCount);
			return actualRead;
		}

		if (data.currentSample*2 + readCount >= MAX_SAMPLES*2)
		{
			//If we are here, there is no data in current chunk. Let's read what we can and go to next chunk

			int toRead = MAX_SAMPLES*2 - data.currentSample*2;

			//std::copy(data.buffer + data.currentSample, data.buffer + data.currentSample + toRead, Buffer + actualRead);
			memcpy(Buffer + actualRead, data.buffer + data.currentSample*2, toRead);

			actualRead += toRead;

			readCount -= toRead;

			audioData->audioData.pop_front();
		}
		else
		{
			//If we are here, there is enough data in current chunk to fill our needs
			//std::copy(data.buffer + data.currentSample, data.buffer + data.currentSample + readCount, Buffer + actualRead);
			memcpy(Buffer + actualRead, data.buffer + data.currentSample*2, readCount);

			data.currentSample += readCount;
			actualRead += readCount;
			readCount = 0;
		}
	}


	audioData->m.unlock();
	actualDurationRead = duration;

	return actualRead;
}


std::vector<int16_t> resample(const std::vector<int16_t>& input, int numChannels, int inputRate, int outputRate) {
	SRC_DATA srcData;
	std::vector<float> inputFloat(input.size());
	std::vector<float> outputFloat(input.size() * outputRate / inputRate + 1);

	// Convert input data to float
	for (size_t i = 0; i < input.size(); ++i) {
		inputFloat[i] = input[i] / 32768.0f;
	}

	srcData.data_in = inputFloat.data();
	srcData.input_frames = input.size() / numChannels;
	srcData.data_out = outputFloat.data();
	srcData.output_frames = outputFloat.size() / numChannels;
	srcData.src_ratio = static_cast<double>(outputRate) / inputRate;
	srcData.end_of_input = 1;

	if (src_simple(&srcData, SRC_SINC_BEST_QUALITY, numChannels) != 0) {
		throw std::runtime_error("Error during resampling");
	}

	std::vector<int16_t> outputData(srcData.output_frames_gen * numChannels);

	// Convert float output to int16_t
	for (size_t i = 0; i < outputData.size(); ++i) {
		outputData[i] = static_cast<int16_t>(std::max(-1.0f, std::min(1.0f, outputFloat[i])) * 32768);
	}

	return outputData;
}


int VoiceDecoderConvertFrom48000::readDuration(char* Buffer, size_t Count, std::chrono::milliseconds duration, std::chrono::milliseconds& actualDurationRead)
{

	int initialReadCount = (NUM_CHANNELS * SAMPLE_RATE * 48000 * duration.count() / 1000);
	int readCount = initialReadCount;

	if (readCount % 2 == 1)
	{
		throw std::runtime_error("Sample must be divisible by 2!");
	}

	//int initialReadCountInShort = initialReadCount / 2;
	//int readCountInShort = readCount / 2;

	int actualRead = 0;

	audioData->m.lock();

	while (readCount > 0)
	{

		if (audioData->audioData.size() == 0)
		{
			// If we are here, there is no audio data left. Return what we had processed
			audioData->m.unlock();
			actualDurationRead = std::chrono::milliseconds(duration.count() * actualRead / initialReadCount);

			return actualRead;
		}

		AudioData& data = audioData->audioData.front();

		if (data.ready == false)
		{
			// If we are here, the next block is not ready yet. Return what we had processed
			audioData->m.unlock();
			int initialMustReadCount = (NUM_CHANNELS * SAMPLE_RATE * 44100 * duration.count() / 1000);
			actualDurationRead = std::chrono::milliseconds(duration.count() * actualRead / initialMustReadCount);
			return actualRead;
		}

		if (data.currentSample * 2 + readCount >= MAX_SAMPLES * 2)
		{
			//If we are here, there is no data in current chunk. Let's read what we can and go to next chunk

			int toRead = MAX_SAMPLES * 2 - data.currentSample * 2;

			std::vector<int16_t> intermediate48000Data;

			intermediate48000Data.resize(toRead);

			memcpy(&intermediate48000Data[0], data.buffer + data.currentSample * 2, toRead);

			std::vector<int16_t> intermediate44100Data;
			intermediate44100Data = resample(intermediate48000Data, NUM_CHANNELS, 48000, 44100);


			memcpy(Buffer + actualRead, &intermediate44100Data[0], intermediate44100Data.size() * 2);

			actualRead += intermediate44100Data.size() * 2;

			readCount -= toRead;

			audioData->audioData.pop_front();
		}
		else
		{
			//If we are here, there is enough data in current chunk to fill our needs
			
			
			//int initialReadCount = (NUM_CHANNELS * SAMPLE_RATE * 48000 * duration.count() / 1000);
			std::vector<int16_t> intermediate48000Data;

			intermediate48000Data.resize(readCount);

			memcpy(&intermediate48000Data[0], data.buffer + data.currentSample * 2, readCount);
			
			std::vector<int16_t> intermediate44100Data;
			intermediate44100Data = resample(intermediate48000Data, NUM_CHANNELS, 48000, 44100);

			
			memcpy(Buffer + actualRead, &intermediate44100Data[0], intermediate44100Data.size() * 2);

			data.currentSample += readCount;
			actualRead += intermediate44100Data.size() * 2;
			readCount = 0;
		}
	}


	audioData->m.unlock();
	actualDurationRead = duration;

	return actualRead;
}