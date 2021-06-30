#pragma once

#include <stdio.h>
#include <chrono>

constexpr size_t BLOCK_SIZE = 16 * 1024;

class AudioDecoder
{
public:
	virtual ~AudioDecoder() = default;

	virtual int open(const char* fileName) = 0;
	virtual int readDuration(char* Buffer, size_t Count, std::chrono::milliseconds duration) = 0;
};
