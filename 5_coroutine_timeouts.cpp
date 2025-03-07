//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancel_after.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/mysql/any_connection.hpp>
#include <boost/mysql/connect_params.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/with_params.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string_view>
#include <system_error>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace mysql = boost::mysql;

namespace {

std::optional<std::uint64_t> try_parse_id(std::string_view request_target)
{
    if (!request_target.starts_with("/"))
        return std::nullopt;
    std::uint64_t res = 0;
    const char* first = request_target.data() + 1;  // skip /
    const char* last = request_target.data() + request_target.size();
    auto result = std::from_chars(first, last, res);
    if (result.ec != std::errc() || result.ptr != last)
        return std::nullopt;
    return res;
}

std::uint64_t parse_id(std::string_view request_target) { return try_parse_id(request_target).value(); }

void log_error(std::exception_ptr exc)
{
    try
    {
        std::rethrow_exception(exc);
    }
    catch (const std::exception& err)
    {
        std::cerr << "Unhandled error: " << err.what() << std::endl;
    }
}

asio::awaitable<http::response<http::string_body>> handle_request(
    const http::request<http::empty_body>& request
)
{
    try
    {
        // Parse the request
        std::uint64_t id = parse_id(request.target());

        // Query the database
        mysql::any_connection conn(co_await asio::this_coro::executor);
        co_await conn.async_connect({.username = "me", .password = "secret", .database = "correlations"});

        mysql::results r;
        co_await conn.async_execute(
            mysql::with_params("SELECT subject FROM correlations WHERE id = {}", id),
            r
        );

        // Compose the response
        http::response<http::string_body> res;
        if (r.rows().empty())
            res.result(http::status::not_found);
        else
            res.body() = r.rows().at(0).at(0).as_string();
        co_return res;
    }
    catch (const std::exception& err)
    {
        // Log the error
        std::cerr << "Unhandled error: " << err.what() << std::endl;

        // Return an unspecific 500 internal server error
        http::response<http::string_body> res;
        res.result(http::status::internal_server_error);
        co_return res;
    }
}

// Runs an individual HTTP session: reads a request,
// processes it, and writes the response.
asio::awaitable<void> run_session(asio::ip::tcp::socket sock)
{
    using namespace std::chrono_literals;

    // Read a request
    beast::flat_buffer buff;
    http::request<http::empty_body> req;
    co_await http::async_read(sock, buff, req, asio::cancel_after(30s));

    // Handle the request
    http::response<http::string_body> res = co_await asio::co_spawn(
        co_await asio::this_coro::executor,
        handle_request(req),
        asio::cancel_after(30s)
    );

    // Write the response back
    res.version(req.version());
    res.keep_alive(false);
    res.prepare_payload();
    co_await http::async_write(sock, res, asio::cancel_after(30s));
}

asio::awaitable<void> run_server()
{
    // An object that allows us to accept incoming TCP connections.
    asio::ip::tcp::acceptor acceptor(co_await asio::this_coro::executor);

    // The endpoint where the server will listen. Edit this if you want to
    // change the address or port we bind to.
    asio::ip::tcp::endpoint listening_endpoint(asio::ip::make_address("0.0.0.0"), 8080);
    acceptor.open(listening_endpoint.protocol());
    acceptor.set_option(asio::socket_base::reuse_address(true));
    acceptor.bind(listening_endpoint);
    acceptor.listen();

    // Accept connections in a loop
    while (true)
    {
        // Accept a connection
        asio::ip::tcp::socket sock = co_await acceptor.async_accept();

        // Launch a session, but don't wait for it
        asio::co_spawn(
            co_await asio::this_coro::executor,
            run_session(std::move(sock)),
            [](std::exception_ptr exc) {
                if (exc)
                    log_error(exc);
            }
        );
    }
}

}  // namespace

int main()
{
    // Execution context. This is a heavyweight object
    // containing all the required infrastructure to run async operations,
    // including a scheduler, timer queues, file descriptors...
    asio::io_context ctx;

    asio::co_spawn(ctx, &run_server, [](std::exception_ptr exc) {
        if (exc)
            std::rethrow_exception(exc);
    });

    ctx.run();
}
