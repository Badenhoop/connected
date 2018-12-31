//
// Created by philipp on 01.12.17.
//

#ifndef PROTOCOL_MESSAGES_H
#define PROTOCOL_MESSAGES_H

#include <cstdint>
#include <vector>
#include <boost/system/error_code.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include "Stream.h"
#include "Networking.h"
#include "Socket.h"
#include <boost/algorithm/string/replace.hpp>

namespace asionet
{
namespace message
{

using SendHandler = std::function<void(const error::ErrorCode & code)>;

template<typename Message>
using ReceiveHandler = std::function<void(const error::ErrorCode & code, std::shared_ptr<Message> & message)>;

using SendToHandler = std::function<void(const error::ErrorCode & code)>;

template<typename Message>
using ReceiveFromHandler = std::function<
	void(const error::ErrorCode & code,
	     std::shared_ptr<Message> & message,
	     const std::string & senderHost,
	     std::uint16_t senderPort)>;

template<typename Message>
struct Encoder;

template<>
struct Encoder<std::string>
{
	std::shared_ptr<std::string> operator()(const std::string & message) const
	{ return std::make_shared<std::string>(message); }
};

template<typename Message>
struct Decoder;

template<>
struct Decoder<std::string>
{
	std::shared_ptr<std::string> operator()(const std::string & data) const
	{ return std::make_shared<std::string>(data); }
};

namespace internal
{

template<typename Message>
bool encode(const Message & message, std::shared_ptr<std::string> & data)
{
	try
	{
		data = Encoder<Message>{}(message);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

template<typename Message>
bool decode(const std::string & data, std::shared_ptr<Message> & message)
{
	try
	{
		message = Decoder<Message>{}(data);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

}

template<typename Message, typename SyncWriteStream>
void send(Networking & net,
          SyncWriteStream & stream,
          const Message & message,
          const time::Duration & timeout)
{
	std::shared_ptr<std::string> data;
	if (!internal::encode(message, data))
		throw error::Encoding{};
	asionet::stream::write(net, stream, *data, timeout);
};

template<typename Message, typename SyncWriteStream>
void asyncSend(Networking & net,
               SyncWriteStream & stream,
               const Message & message,
               const time::Duration & timeout,
               const SendHandler & handler)
{
	std::shared_ptr<std::string> data;
	if (!internal::encode(message, data))
	{
		net.callLater(
			[handler] { handler(error::codes::ENCODING); });
		return;
	}

	asionet::stream::asyncWrite(
		net, stream, data, timeout,
		[handler, data](const auto & errorCode) { handler(errorCode); });
};

template<typename Message, typename SyncReadStream>
std::shared_ptr<Message> receive(Networking & net,
                                 SyncReadStream & stream,
                                 boost::asio::streambuf & buffer,
                                 const time::Duration & timeout)
{
	auto data = asionet::stream::read(net, stream, buffer, timeout);
	std::shared_ptr<Message> message;
	if (!internal::decode(data, message))
		throw error::Decoding{};
	return message;
};

template<typename Message, typename SyncReadStream>
void asyncReceive(Networking & net,
                  SyncReadStream & stream,
                  boost::asio::streambuf & buffer,
                  const time::Duration & timeout,
                  const ReceiveHandler<Message> & handler)
{
	asionet::stream::asyncRead(
		net, stream, buffer, timeout,
		[handler](const auto & errorCode, const auto & data)
		{
			std::shared_ptr<Message> message;
			if (!internal::decode(data, message))
			{
				handler(error::codes::DECODING, message);
				return;
			}
			handler(errorCode, message);
		});
};

template<typename Message, typename DatagramSocket>
void sendDatagram(Networking & net,
                  DatagramSocket & socket,
                  const Message & message,
                  const std::string & host,
                  std::uint16_t port,
                  const time::Duration & timeout)
{
	std::shared_ptr<std::string> data;
	if (!internal::encode(message, data))
		throw error::Encoding{};
	asionet::socket::sendTo(net, socket, *data, host, port, timeout);
}

template<typename Message, typename DatagramSocket>
void asyncSendDatagram(Networking & net,
                       DatagramSocket & socket,
                       const Message & message,
                       const std::string & host,
                       std::uint16_t port,
                       const time::Duration & timeout,
                       const SendToHandler & handler)
{
	std::shared_ptr<std::string> data;
	if (!internal::encode(message, data))
	{
		net.callLater(
			[handler] { handler(error::codes::ENCODING); });
		return;
	}

	asionet::socket::asyncSendTo(
		net, socket, data, host, port, timeout,
		[handler, data](const auto & error) { handler(error); });
}

template<typename Message, typename DatagramSocket>
std::shared_ptr<Message> receiveDatagram(Networking & net,
                                         DatagramSocket & socket,
                                         std::vector<char> & buffer,
                                         std::string & host,
                                         std::uint16_t & port,
                                         const time::Duration & timeout)
{
	auto data = asionet::socket::receiveFrom(net, socket, buffer, host, port, timeout);
	std::shared_ptr<Message> message;
	if (!internal::decode(data, message))
		throw error::Decoding{};
	return message;
}

template<typename Message, typename DatagramSocket>
void asyncReceiveDatagram(Networking & net,
                          DatagramSocket & socket,
                          std::vector<char> & buffer,
                          const time::Duration & timeout,
                          const ReceiveFromHandler<Message> & handler)
{
	asionet::socket::asyncReceiveFrom(
		net, socket, buffer, timeout,
		[handler](auto error, const auto & data, const auto & senderHost, auto senderPort)
		{
			std::shared_ptr<Message> message;
			if (!internal::decode(data, message))
			{
				handler(error::codes::DECODING, message, senderHost, senderPort);
				return;
			}
			handler(error, message, senderHost, senderPort);
		});
}

}
}


#endif //PROTOCOL_MESSAGES_H
