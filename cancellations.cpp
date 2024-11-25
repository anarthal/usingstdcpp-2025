//
// Copyright (c) 2019-2024 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

/**
 * Sample code for the talk
 * "Cancellations in Asio: a tale of coroutines and timeouts",
 * proposed for using std::cpp 2025. This is still a work in progress.
 *
 * This program implements a simplistic HTTP server that accesses
 * a SQL database when handling client requests.
 * Recognizes requests with the form GET /employee/{id},
 * where id is an integral number identifying an employee.
 * It returns a plaintext body with the employee's last name.
 *
 * The main point of this server is learning about
 * per-operation cancellation in Asio.
 * Boost.Beast and Boost.MySQL are used to make
 * the example more realistic.
 */

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancel_after.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/with_params.hpp>
#include <boost/system/error_code.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace mysql = boost::mysql;
using boost::system::error_code;

namespace {

// Helper function to log unhandled exceptions
// when handling requests
void log_exception(std::exception_ptr exc)
{
    try
    {
        std::rethrow_exception(exc);
    }
    catch (const std::exception& err)
    {
        std::cerr << "Unhandled exception: " << err.what() << std::endl;
    }
}

// Validates an incoming HTTP request, extracting the employee ID that the client
// is asking for. If the verb or target don't match what we expect,
// returns an empty optional.
// A more refined version could return a std::expected/boost::system::result
// containing what went wrong, so we can return specialized responses
// (e.g. http::verb::method_not_allowed if the method is not what we expected).
std::optional<std::int64_t> parse_request(const http::request<http::empty_body>& req)
{
    constexpr std::string_view prefix = "/employee/";

    // Check the verb
    if (req.method() != http::verb::get)
        return {};

    // Check that the target starts with the prefix
    auto target = req.target();
    if (!target.starts_with(prefix))
        return {};

    // Attempt to parse the ID following the prefix
    std::int64_t res{};
    auto from_chars_res = std::from_chars(target.begin() + prefix.size(), target.end(), res);
    if (from_chars_res.ec != std::errc{})
        return {};
    if (from_chars_res.ptr != target.end())
        return {};

    // Done
    return res;
}

// Handles an individual HTTP request.
// This function accesses the SQL database, performing async operations,
// so it's a C++20 coroutine. Coroutines in Asio return asio::awaitable<T>,
// where T is the type to co_return from the coroutine.
// We will set a timeout to the entire coroutine (see the call site).
asio::awaitable<http::response<http::string_body>> handle_request(
    mysql::connection_pool& pool,               // contains connections to the database
    const http::request<http::empty_body>& req  // HTTP request
)
{
    // The response to return
    http::response<http::string_body> res;

    try
    {
        // Parse the request
        std::optional<std::int64_t> employee_id = parse_request(req);
        if (!employee_id)
        {
            res.result(http::status::bad_request);  // HTTP 400
            co_return res;
        }

        // Get a connection to the database server from the pool.
        // If no connection is available, this will wait one is ready.
        mysql::pooled_connection conn = co_await pool.async_get_connection();

        // Query the database using dynamic SQL
        mysql::results query_result;
        co_await conn->async_execute(
            mysql::with_params("SELECT last_name FROM employee WHERE id = {}", employee_id),
            query_result
        );

        // If the query didn't get any row back, return a 404
        if (query_result.rows().empty())
        {
            res.result(http::status::not_found);
            co_return res;
        }

        // Return the response
        res.body() = query_result.rows().at(0).at(0).as_string();
        co_return res;
    }
    catch (const std::exception& err)
    {
        // If any of the above operations encountered an error,
        // an exception is thrown. This can happen if the server
        // is unhealthy, the server returns an error when running the query,
        // or the coroutine gets cancelled.
        std::cerr << "Error while handling request: " << err.what() << std::endl;
        res.result(http::status::internal_server_error);  // 500 error
        co_return res;
    }
}

// Runs an individual HTTP session: reads a request,
// processes it, and writes the response.
asio::awaitable<void> run_session(mysql::connection_pool& pool, asio::ip::tcp::socket sock)
{
    using namespace std::chrono_literals;

    // Read a request. We say that http::async_read is an Asio composed operation:
    // it calls asio::ip::tcp::socket::async_read_some() several times, until
    // the entire HTTP request is read.
    // The last argument to http::async_read() is the completion token:
    // it specifies what to do when the async operation completes.
    // If nothing is specified, Asio returns an object that can be co_await'ed.
    // asio::cancel_after is a completion token that can be used to specify timeouts:
    // if the operation does not complete in 60 seconds, a cancellation is issued,
    // and the operation finishes with an error.
    // By default, asio::cancel_after() also turns the operation into an awaitable.
    beast::flat_buffer buff;
    http::request<http::empty_body> req;
    co_await http::async_read(sock, buff, req, asio::cancel_after(60s));

    // Handle the request. We want to limit the overall time taken by
    // the request to 30 seconds.
    // If we had written "co_await handle_request(pool, req)", we would
    // have had no way to specify the timeout.
    // In this sense, we can classify async operations in Asio into two types:
    //    - co_await handle_request(pool, req) always uses C++20 coroutines.
    //      These are easy to write, but less flexible.
    //    - http::async_read(), asio::co_spawn() and other library functions
    //      follow Asio's universal async model. That is, they can be passed
    //      a completion token as last parameter. These are more difficult to
    //      write, but are more flexible.
    // asio::co_spawn() is actually an async operation, too, so we can co_await it.
    // If the timeout elapses and handle_request hasn't finished, the async
    // operation that handle_request() is waiting for will be cancelled.
    // This makes it finish with an error (similar to when a network error occurs).
    // Note that a cancellation does NOT make the coroutine to "just stop executing".
    http::response<http::string_body> res = co_await asio::co_spawn(
        // Use the same executor as the parent coroutine.
        // An executor represents a handle to an execution context (i.e. event loop)
        co_await asio::this_coro::executor,

        // The coroutine to actually execute
        [&] { return handle_request(pool, req); },

        // The completion token for the coroutine
        asio::cancel_after(30s)
    );

    // Send the response, specifying a timeout.
    // More complex versions could support HTTP keep alive, handling requests
    // in a loop.
    res.version(req.version());
    res.keep_alive(false);
    res.prepare_payload();
    co_await http::async_write(sock, res, asio::cancel_after(60s));
}

// The main coroutine
asio::awaitable<void> listener(mysql::connection_pool& pool, unsigned short port)
{
    // An object that allows us to accept incoming TCP connections.
    asio::ip::tcp::acceptor acceptor(co_await asio::this_coro::executor);

    // The endpoint where the server will listen. Edit this if you want to
    // change the address or port we bind to.
    asio::ip::tcp::endpoint listening_endpoint(asio::ip::make_address("0.0.0.0"), port);

    // Open the acceptor
    acceptor.open(listening_endpoint.protocol());

    // Allow address reuse
    acceptor.set_option(asio::socket_base::reuse_address(true));

    // Bind to the server address
    acceptor.bind(listening_endpoint);

    // Start listening for connections
    acceptor.listen();
    std::cout << "Server listening at " << acceptor.local_endpoint() << std::endl;

    // Accept connections in a loop
    while (true)
    {
        // Accept a connection
        auto sock = co_await acceptor.async_accept();

        // Launch a session.
        // Don't co_await run_session: we want to keep accepting connections
        // while a session is in progress.
        // This time, we're passing a callback to co_spawn,
        // which is a valid completion token, too.
        // The callback will be called when the coroutine completes.
        asio::co_spawn(
            co_await asio::this_coro::executor,
            [socket = std::move(sock), &pool]() mutable { return run_session(pool, std::move(socket)); },
            [](std::exception_ptr exc) {
                if (exc)
                    log_exception(exc);
            }
        );
    }
}

}  // namespace

int main(int argc, char** argv)
{
    // Check command line arguments.
    if (argc != 5)
    {
        std::cerr << "Usage: " << argv[0] << " <db-username> <db-password> <db-hostname> <http-port>\n";
        return EXIT_FAILURE;
    }
    auto port = static_cast<unsigned short>(std::stoi(argv[4]));

    // Execution context. This is a heavyweight object
    // containing all the required infrastructure to run async operations,
    // including a scheduler, timer queues, file descriptors...
    asio::io_context ctx;

    // Launch the MySQL pool
    mysql::connection_pool pool(
        ctx,
        {
            .server_address = mysql::host_and_port(argv[3]),
            .username = argv[1],
            .password = argv[2],
            .database = "usingstdcpp",
        }
    );
    pool.async_run(asio::detached);

    // Start listening for HTTP connections
    asio::co_spawn(
        ctx,
        [&pool, port] { return listener(pool, port); },
        [](std::exception_ptr exc) {
            if (exc)
                std::rethrow_exception(exc);
        }
    );

    // Capture SIGINT and SIGTERM to perform a clean shutdown
    asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait([&ctx](error_code, int) {
        // Stop the execution context. This will cause run() to exit
        ctx.stop();
    });

    // Run until stopped
    ctx.run();
}
