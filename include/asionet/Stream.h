/*
 * The MIT License
 * 
 * Copyright (c) 2019 Philipp Badenhoop
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef ASIONET_STREAM_H
#define ASIONET_STREAM_H

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio.hpp>
#include "Timer.h"
#include "Error.h"
#include "Closeable.h"
#include "Frame.h"
#include "Utils.h"

namespace asionet
{
namespace stream
{

namespace internal
{

inline std::uint32_t numDataBytesFromBuffer(boost::asio::streambuf & streambuf)
{
    auto numDataBytesStr = utils::stringFromStreambuf(streambuf, 4);
    return utils::fromBigEndian<4, std::uint32_t>((const std::uint8_t *) numDataBytesStr.c_str());
}

}

using WriteHandler = std::function<void(const error::Error & error)>;

using ReadHandler = std::function<void(const error::Error & error, std::string & data)>;

template<typename SyncWriteStream>
void asyncWrite(SyncWriteStream & stream,
                const std::string & writeData,
                const time::Duration & timeout,
                WriteHandler handler = [] (auto && ...) {})
{
    using namespace asionet::internal;
    auto frame = std::make_shared<Frame>((const std::uint8_t *) writeData.c_str(), writeData.size());
    auto buffers = frame->getBuffers();

    auto asyncOperation = [](auto && ... args) { boost::asio::async_write(std::forward<decltype(args)>(args)...); };

    closeable::timedAsyncOperation(
        asyncOperation, stream, timeout,
        [handler = std::move(handler), frame = std::move(frame)](const auto & error, auto numBytesTransferred)
        {
            if (numBytesTransferred < frame->getSize())
            {
                handler(error::failedOperation);
                return;
            }

            handler(error);
        },
        stream, buffers);
}

template<typename SyncReadStream>
void asyncRead(SyncReadStream & stream,
               boost::asio::streambuf & buffer,
               const time::Duration & timeout,
               ReadHandler handler)
{
    using asionet::internal::Frame;
    using namespace asionet::stream::internal;

    auto startTime = time::now();

    auto asyncOperation = [](auto && ... args) { boost::asio::async_read(std::forward<decltype(args)>(args)...); };

    // Receive frame header.
    closeable::timedAsyncOperation(
        asyncOperation, stream, timeout,
        [&stream, &buffer, timeout, handler = std::move(handler), startTime]
            (const auto & error, auto numBytesTransferred)
        {
            std::string data{};

            if (error)
            {
                handler(error, data);
                return;
            }

            if (numBytesTransferred != Frame::HEADER_SIZE)
            {
                handler(error::invalidFrame, data);
                return;
            }

            auto numDataBytes = numDataBytesFromBuffer(buffer);
            if (numDataBytes == 0)
            {
                handler(error, data);
                return;
            }

            auto timeSpend = time::now() - startTime;
            auto newTimeout = timeout - timeSpend;

	        auto asyncOperation = [](auto && ... args) { boost::asio::async_read(std::forward<decltype(args)>(args)...); };

            // Receive actual data.
            closeable::timedAsyncOperation(
                asyncOperation, stream, newTimeout,
                [&buffer, handler = std::move(handler), numDataBytes]
                    (const auto & error, auto numBytesTransferred)
                {
                    std::string data{};

                    if (error)
                    {
                        handler(error, data);
                        return;
                    }

                    if (numBytesTransferred != numDataBytes)
                    {
                        handler(error::invalidFrame, data);
                        return;
                    }

                    data = utils::stringFromStreambuf(buffer, numDataBytes);
                    handler(error, data);
                },
                stream, buffer, boost::asio::transfer_exactly(numDataBytes));
        },
        stream, buffer, boost::asio::transfer_exactly(Frame::HEADER_SIZE));
};

}
}

#endif //ASIONET_STREAM_H
