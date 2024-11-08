#ifndef FILE_UPLOADER
#define FILE_UPLOADER

#include <iostream>
#include <memory>
#include <mutex>
#include "boost/asio.hpp"
#include "boost/filesystem.hpp"
#include <vector>
#include <string>
#include <future>
#include "Voice.h"

//OpenSSL stuff -- Vladislav Khorev vladislav.khorev@fishrungames.com
//#define SSL_R_SHORT_READ 219
//#include "ssl/ssl_locl.h"
//#include <boost/asio/ssl.hpp>

#if defined(close)
#undef close
#endif

#if defined(open)
#undef open
#endif

struct ContentToStream
{
	std::vector<std::string> playlist;

	ContentToStream() = default;

	ContentToStream(const std::string& singleSong)
		: playlist( { singleSong } )
	{
	}

	ContentToStream(const std::vector<std::string>& newPlaylist)
		: playlist (newPlaylist)
	{
	}
};

struct CrossFadeSelector
{
	virtual double audioDataVolume() = 0;
	virtual double voiceVolume() = 0;
	virtual void update() = 0;
};

struct ContentToStreamWithAudio
{
	CrossFadeSelector& crossFadeSelector;

	std::vector<std::string> playlist;

	std::shared_ptr<AudioDataList> audioData;


	ContentToStreamWithAudio(CrossFadeSelector& inCrossFadeSelector, const std::vector<std::string>& newPlaylist)
		: crossFadeSelector(inCrossFadeSelector)
		, playlist(newPlaylist)
	{
	}
};

struct Uploading
{
	ContentToStream contentToStream;
	std::string addres;
	std::string port;
};


class IcecastStreamer
{
public:
	/*
	enum class AudioFormat
	{
		Invalid,
		WAV,
		MP3,
		AAC,
		M4A
	};*/

	std::string addres;
	std::string port;

	boost::asio::io_service& io_service;

	IcecastStreamer(boost::asio::io_service& ioService, std::string addres, std::string port);

	//void streamFile(const ContentToStream& contentToStream, std::shared_ptr<std::promise<void>> promise);
	//void streamFile(boost::asio::ip::tcp::endpoint endpoint, const ContentToStream& contentToStream, std::shared_ptr<std::promise<void>> promise);

	//bool streamFileInner(std::shared_ptr<boost::asio::ip::tcp::socket> socket, const Uploading& uploading);


	void streamFileLooped(const ContentToStream& contentToStream, std::shared_ptr<std::promise<void>> promise);
	void streamFileLooped(boost::asio::ip::tcp::endpoint endpoint, const ContentToStream& contentToStream, std::shared_ptr<std::promise<void>> promise);
	bool streamFileLoopedInner(std::shared_ptr<boost::asio::ip::tcp::socket> socket, const Uploading& uploading);

	void streamVoice(std::shared_ptr<AudioDataList> audioData, std::shared_ptr<std::promise<void>> promise);
	void streamVoice(boost::asio::ip::tcp::endpoint endpoint, std::shared_ptr<AudioDataList> audioData, std::shared_ptr<std::promise<void>> promise);
	bool streamVoiceInner(std::shared_ptr<boost::asio::ip::tcp::socket> socket, std::shared_ptr<AudioDataList> audioData);

	void streamComplexLooped(const ContentToStreamWithAudio& contentToStream, std::shared_ptr<std::promise<void>> promise);
	void streamComplexLooped(boost::asio::ip::tcp::endpoint endpoint, const ContentToStreamWithAudio& contentToStream, std::shared_ptr<std::promise<void>> promise);
	bool streamComplexLoopedInner(std::shared_ptr<boost::asio::ip::tcp::socket> socket, const ContentToStreamWithAudio& contentToStream);

	//std::vector<std::string> downloadPlaylist();
	//std::vector<std::string> loadPlaylistFromFile();
};

#endif