﻿#include "IcecastStreamer.h"

#include <conio.h>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <wave/WaveFile.h>
#include <wave/WaveDecoder.h>
#include <mp3/Mp3Decoder.h>
#include <aac/AacDecoder.h>

#include <random>
#include <algorithm>
#include <iterator>
#include <iostream>


#include <id3/tag.h>

#include "tags/Tags.h"



#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>

std::string url_encode(const std::string &value) {

	using namespace std;

	ostringstream escaped;
	escaped.fill('0');
	escaped << hex;

	for (string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
		string::value_type c = (*i);

		// Keep alphanumeric and other accepted characters intact
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			escaped << c;
			continue;
		}

		// Any other characters are percent-encoded
		escaped << uppercase;
		escaped << '%' << setw(2) << int((unsigned char)c);
		escaped << nouppercase;
	}

	return escaped.str();
}

struct ID3Metadata {

	std::string title;
	std::string artist;

};

ID3Metadata getMetadata(const std::string& filename)
{
	ID3Metadata result{ "Unknown", "Unknown" };

	/*
	KTags tags;
	TelError err = ReadTagsFromFile(filename.c_str(), tags);
	if (err != TEL_ERR_OK)
	{
		
		return result;
	}
	result.title = tags.GetTag(TAG_TITLE).GetAsUtf8();

	result.artist = tags.GetTag(TAG_ARTIST).GetAsUtf8();
	*/
	return result;
}




const long long MAX_UPLOADED_FILE_SIZE = 1024 * 1024 * 400;


IcecastStreamer::IcecastStreamer(boost::asio::io_service& ioService, std::string addres, std::string port)
	: io_service(ioService)
{
	this->addres = addres;
	this->port = port;
}


void IcecastStreamer::streamFile(const ContentToStream& contentToStream, std::shared_ptr<std::promise<void>> promise)
{
	boost::asio::ip::tcp::resolver resolver(io_service);
	boost::asio::ip::tcp::resolver::query query(addres, port);
	
	boost::system::error_code errcode;
	boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve(query, errcode);

	if (errcode)
	{
		std::cout << "IcecastStreamer::postUploadFileHttp: couldn't resolve addres, retry..." << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(1));
		io_service.post([contentToStream, promise, this]() { streamFile(contentToStream, promise); });
		return;
	}

	io_service.post([endpoint, contentToStream, promise, this]()
	{
		streamFile(endpoint, contentToStream, promise);
	});
}


//bool streamFileInner(std::shared_ptr<boost::asio::ip::tcp::socket> socket, const Uploading& uploading);

void IcecastStreamer::streamFile(boost::asio::ip::tcp::endpoint endpoint, const ContentToStream& contentToStream, std::shared_ptr<std::promise<void>> promise)
{
	std::shared_ptr<boost::asio::ip::tcp::socket> httpSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_service);

	boost::system::error_code errcode;
	httpSocket->connect(endpoint, errcode);

	if (errcode)
	{
		std::cout << "IcecastStreamer::streamFile: couldn't connect to the server, retry..." << std::endl;
		httpSocket->lowest_layer().close();
		std::this_thread::sleep_for(std::chrono::seconds(1));
		io_service.post([endpoint, contentToStream, promise, this]() { streamFile(endpoint, contentToStream, promise); });
		return;
	}

	Uploading uploading;
	uploading.addres = addres;
	uploading.port = port;
	uploading.contentToStream = contentToStream;

	if (!streamFileInner(httpSocket, uploading))
	{
		httpSocket->lowest_layer().close();
		std::this_thread::sleep_for(std::chrono::seconds(1));
		io_service.post([endpoint, contentToStream, promise, this]() { streamFile(endpoint, contentToStream, promise); });
	}
	else
	{
		promise->set_value();
	}
}

std::shared_ptr<AudioDecoder> createReader(const std::string& fileName)
{
	auto ext = boost::filesystem::extension(fileName);

	std::shared_ptr<AudioDecoder> reader;

	IcecastStreamer::AudioFormat format = IcecastStreamer::AudioFormat::Invalid;
	if (ext == ".wav")
	{
		format = IcecastStreamer::AudioFormat::WAV;
		reader = std::make_shared<Decoding::WaveToMp3Decoder>();
	}
	else if (ext == ".mp3")
	{
		format = IcecastStreamer::AudioFormat::MP3;
		reader = std::make_shared<Decoding::Mp3WaveMp3Decoder>();
	}
	else if (ext == ".aac" || ext == ".m4a" || ext == ".mp4")
	{
		format = IcecastStreamer::AudioFormat::AAC;
		reader = std::make_shared<Decoding::AacToMp3Decoder>();
	}

	if (format == IcecastStreamer::AudioFormat::Invalid)
	{
		return false;
	}

	if (!reader->open(fileName.c_str()))
	{
		std::cout << "IcecastStreamer: couldn't open the file to be streamed" << std::endl;
		return nullptr;
	}

	return reader;
}


bool streamOneReader(std::shared_ptr<boost::asio::ip::tcp::socket> socket, std::shared_ptr<AudioDecoder> reader)
{

	std::vector<char> Buffer;
	Buffer.resize(1024 * 1024);

	int packet = 0;

	int byteCount;

	while (true)
	{

		std::chrono::time_point<std::chrono::system_clock> nowBefore = std::chrono::system_clock::now();

		byteCount = reader->readDuration(&Buffer[0], Buffer.size(), std::chrono::seconds(3));

		if (byteCount < 1)
		{
			break;
		}

		auto asioBuffer = boost::asio::buffer(Buffer, byteCount);

		try
		{
			socket->send(asioBuffer);
			std::cout << "IcecastStreamer: streaming... " << ++packet << " : " << byteCount << std::endl;
		}
		catch (std::exception &e)
		{
			std::cout << "IcecastStreamer: connection issues, retry..." << std::endl;
			return false;
		}

		std::chrono::time_point<std::chrono::system_clock> nowAfter = std::chrono::system_clock::now();

		auto duration = std::chrono::seconds(3) - (nowAfter - nowBefore);

		std::this_thread::sleep_for(duration);
	}

	return true;
}

bool updateMetadata(boost::asio::io_service& io_service, const std::string& address, const std::string& port, const Uploading& uploading, const ID3Metadata& metadata)
{
	boost::asio::ip::tcp::resolver resolver(io_service);
	boost::asio::ip::tcp::resolver::query query(address, port);

	boost::system::error_code errcode;
	boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve(query, errcode);

	std::shared_ptr<boost::asio::ip::tcp::socket> httpSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_service);

	httpSocket->connect(endpoint, errcode);

	if (errcode)
	{
		std::cout << "IcecastStreamer::streamFile: couldn't connect to the server, retry..." << std::endl;
		httpSocket->lowest_layer().close();
		return false;
	}

	const std::string NEWLINE = "\r\n";

	boost::asio::streambuf request;
	std::ostream request_stream(&request);

	boost::asio::streambuf response;
	std::istream response_stream(&response);

	//std::string metadataString = metadata.artist + " - " + metadata.title;
	std::string metadataString = url_encode(metadata.artist + " - " + metadata.title);

	
	boost::erase_all(metadataString, "\n");
	boost::erase_all(metadataString, "\r");

	request_stream << "GET /admin/metadata?mount=/test.mp3&mode=updinfo&song=" + metadataString + " HTTP/1.1" << NEWLINE;
	request_stream << "Host: " << uploading.addres << ":" << uploading.port << NEWLINE;
	request_stream << "User-Agent: IcecastTestStreamer" << NEWLINE;
	request_stream << "Authorization: Basic YWRtaW46aGFja21l" << NEWLINE;

	request_stream << NEWLINE;

	try
	{
		httpSocket->send(buffer(request.data(), request.size()));

		int byteCount = boost::asio::read_until(*httpSocket, response, '\r') - 1;

		std::string responseCode(byteCount, ' ');
		response_stream.read(&responseCode[0], byteCount);

		std::cout << "Icecast Server Response: " << responseCode << std::endl;

		if (responseCode.find("200 OK") == std::string::npos)
		{
			return false;
		}
	}
	catch (std::exception &e)
	{
		std::cout << "IcecastStreamer: connection issues, retry..." << std::endl;
		return false;
	}

	return true;
}



bool IcecastStreamer::streamFileInner(std::shared_ptr<boost::asio::ip::tcp::socket> socket, const Uploading& uploading)
{
	const std::string NEWLINE = "\r\n";

	boost::asio::streambuf request;
	std::ostream request_stream(&request);

	boost::asio::streambuf response;
	std::istream response_stream(&response);

	
		request_stream << "PUT /test.mp3 HTTP/1.1" << NEWLINE;
		request_stream << "Host: " << uploading.addres << ":" << uploading.port << NEWLINE;
		request_stream << "User-Agent: IcecastTestStreamer" << NEWLINE;
		request_stream << "Transfer-Encoding: chunked" << NEWLINE;
		request_stream << "Content-Type: audio/mpeg" << NEWLINE;
		//request_stream << "Content-Type: audio/vnd.wave" << NEWLINE;
		request_stream << "Expect: 100-continue" << NEWLINE;
		request_stream << "Authorization: Basic c291cmNlOnNvdXJjZV9wYXNzd29yZA==" << NEWLINE;

		request_stream << "Ice-Public: 1" << NEWLINE;
		request_stream << "Ice-Name: test_stream" << NEWLINE;
		request_stream << "Ice-Description: Hello, World!" << NEWLINE;

		request_stream << NEWLINE;

		try
		{
			socket->send(buffer(request.data(), request.size()));

			int byteCount = boost::asio::read_until(*socket, response, '\r') - 1;

			std::string responseCode(byteCount, ' ');
			response_stream.read(&responseCode[0], byteCount);

			std::cout << "Icecast Server Response: " << responseCode << std::endl;

			if (responseCode.find("100 Continue") == std::string::npos)
			{
				return false;
			}
		}
		catch (std::exception &e)
		{
			std::cout << "IcecastStreamer: connection issues, retry..." << std::endl;
			return false;
		}
	
/*
	auto ext = boost::filesystem::extension(uploading.fileName);

	std::shared_ptr<AudioDecoder> reader;

	IcecastStreamer::AudioFormat format = IcecastStreamer::AudioFormat::Invalid;
	if (ext == ".wav")
	{
		format = IcecastStreamer::AudioFormat::WAV;
		reader = std::make_shared<Decoding::WaveToMp3Decoder>();
	}
	else if (ext == ".mp3")
	{
		format = IcecastStreamer::AudioFormat::MP3;
		reader = std::make_shared<Decoding::Mp3WaveMp3Decoder>();
	}
	else if (ext == ".aac" || ext == ".m4a" || ext == ".mp4")
	{
		format = IcecastStreamer::AudioFormat::AAC;
		reader = std::make_shared<Decoding::AacToMp3Decoder>();
	}

	if (format == IcecastStreamer::AudioFormat::Invalid)
	{
		return false;
	}

	if (!reader->open(uploading.fileName.c_str()))
	{
		std::cout << "IcecastStreamer: couldn't open the file to be streamed" << std::endl;
		return false;
	}
	*/



	while (true)
	{
		auto shuffledPlaylist = uploading.contentToStream.playlist;
		

		std::random_device rd;
		std::mt19937 g(rd());

		std::shuffle(shuffledPlaylist.begin(), shuffledPlaylist.end(), g);
		
		for (size_t i = 0; i < shuffledPlaylist.size(); i++)
		{
			std::string prefix = "D:/music/";
			std::string fileName = prefix + shuffledPlaylist[i];
			auto reader = createReader(fileName);

			ID3Metadata metadata = getMetadata(fileName);

			updateMetadata(io_service, this->addres, this->port, uploading, metadata);

			if (!reader)
			{
				std::cout << "File can't be played: " << shuffledPlaylist[i] << std::endl;
			}
			else
			{

				if (streamOneReader(socket, reader))
				{
					std::cout << "File played successfully: " << shuffledPlaylist[i] << std::endl;
				}
				else
				{
					std::cout << "Error when playing file: " << shuffledPlaylist[i] << std::endl;
				}
			}
		}
	}

	std::cout << "IcecastStreamer: stream is finished" << std::endl;

	return true;
}

