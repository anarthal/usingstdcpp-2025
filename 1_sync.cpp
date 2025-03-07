//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/mysql/any_connection.hpp>
#include <boost/mysql/connect_params.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/with_params.hpp>

#include <charconv>
#include <cstdint>
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

// Runs an individual HTTP session: reads a request,
// processes it, and writes the response.
void run_session(asio::ip::tcp::socket& sock)
{
    // Read a request
    beast::flat_buffer buff;
    http::request<http::empty_body> req;
    http::read(sock, buff, req);
    std::uint64_t id = parse_id(req.target());

    // Handle the request
    mysql::any_connection conn(sock.get_executor());
    conn.connect(mysql::connect_params{.username = "me", .password = "secret", .database = "correlations"});

    mysql::results r;
    conn.execute(mysql::with_params("SELECT subject FROM correlations WHERE id = {}", id), r);
    std::string_view name = r.rows().at(0).at(0).as_string();

    // Return the response
    http::response<http::string_body> res(http::status::ok, req.version());
    res.body() = name;
    res.keep_alive(false);
    res.prepare_payload();
    http::write(sock, res);
}

}  // namespace

int main()
{
    // Execution context. This is a heavyweight object
    // containing all the required infrastructure to run async operations,
    // including a scheduler, timer queues, file descriptors...
    asio::io_context ctx;

    // An object that allows us to accept incoming TCP connections.
    asio::ip::tcp::acceptor acceptor(ctx);

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
        asio::ip::tcp::socket sock = acceptor.accept();

        // Launch a session.
        run_session(sock);
    }
}
