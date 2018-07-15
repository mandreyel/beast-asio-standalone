//
// Copyright (c) 2016-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: HTTP SSL client, coroutine
//
//------------------------------------------------------------------------------

#include "example/common/root_certificates.hpp"

#include <beast/core.hpp>
#include <beast/http.hpp>
#include <beast/version.hpp>
#include <asio/connect.hpp>
#include <asio/spawn.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ssl/error.hpp>
#include <asio/ssl/stream.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

using tcp = asio::ip::tcp;       // from <asio/ip/tcp.hpp>
namespace ssl = asio::ssl;       // from <asio/ssl.hpp>
namespace http = beast::http;    // from <beast/http.hpp>

//------------------------------------------------------------------------------

// Report a failure
void
fail(std::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

// Performs an HTTP GET and prints the response
void
do_session(
    std::string const& host,
    std::string const& port,
    std::string const& target,
    int version,
    asio::io_context& ioc,
    ssl::context& ctx,
    asio::yield_context yield)
{
    std::error_code ec;

    // These objects perform our I/O
    tcp::resolver resolver{ioc};
    ssl::stream<tcp::socket> stream{ioc, ctx};

    // Set SNI Hostname (many hosts need this to handshake successfully)
    if(! SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
    {
        ec.assign(static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category());
        std::cerr << ec.message() << "\n";
        return;
    }

    // Look up the domain name
    auto const results = resolver.async_resolve(host, port, yield[ec]);
    if(ec)
        return fail(ec, "resolve");

    // Make the connection on the IP address we get from a lookup
    asio::async_connect(stream.next_layer(), results.begin(), results.end(), yield[ec]);
    if(ec)
        return fail(ec, "connect");

    // Perform the SSL handshake
    stream.async_handshake(ssl::stream_base::client, yield[ec]);
    if(ec)
        return fail(ec, "handshake");

    // Set up an HTTP GET request message
    http::request<http::string_body> req{http::verb::get, target, version};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BEAST_VERSION_STRING);

    // Send the HTTP request to the remote host
    http::async_write(stream, req, yield[ec]);
    if(ec)
        return fail(ec, "write");

    // This buffer is used for reading and must be persisted
    beast::flat_buffer b;

    // Declare a container to hold the response
    http::response<http::dynamic_body> res;

    // Receive the HTTP response
    http::async_read(stream, b, res, yield[ec]);
    if(ec)
        return fail(ec, "read");

    // Write the message to standard out
    std::cout << res << std::endl;

    // Gracefully close the stream
    stream.async_shutdown(yield[ec]);
    if(ec == asio::error::eof)
    {
        // Rationale:
        // http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
        ec.assign(0, ec.category());
    }
    if(ec)
        return fail(ec, "shutdown");

    // If we get here then the connection is closed gracefully
}

//------------------------------------------------------------------------------

int main(int argc, char** argv)
{
    // Check command line arguments.
    if(argc != 4 && argc != 5)
    {
        std::cerr <<
            "Usage: http-client-coro-ssl <host> <port> <target> [<HTTP version: 1.0 or 1.1(default)>]\n" <<
            "Example:\n" <<
            "    http-client-coro-ssl www.example.com 443 /\n" <<
            "    http-client-coro-ssl www.example.com 443 / 1.0\n";
        return EXIT_FAILURE;
    }
    auto const host = argv[1];
    auto const port = argv[2];
    auto const target = argv[3];
    int version = argc == 5 && !std::strcmp("1.0", argv[4]) ? 10 : 11;

    // The io_context is required for all I/O
    asio::io_context ioc;

    // The SSL context is required, and holds certificates
    ssl::context ctx{ssl::context::sslv23_client};

    // This holds the root certificate used for verification
    load_root_certificates(ctx);

    // Launch the asynchronous operation
    asio::spawn(ioc, std::bind(
        &do_session,
        std::string(host),
        std::string(port),
        std::string(target),
        version,
        std::ref(ioc),
        std::ref(ctx),
        std::placeholders::_1));

    // Run the I/O service. The call will return when
    // the get operation is complete.
    ioc.run();

    return EXIT_SUCCESS;
}
