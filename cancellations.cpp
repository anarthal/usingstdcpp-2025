
#include <boost/mysql/connection_pool.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace mysql = boost::mysql;
using boost::system::error_code;

std::int64_t parse_request(const http::request<http::string_body>& req)
{
    // TODO
    return 0;
}

asio::awaitable<std::optional<std::string>> get_company(std::int64_t employee_id)
{
    // TODO
    co_return "";
}

http::response<http::string_body> compose_response(const std::optional<std::string>& company)
{
    // TODO
    return {};
}

static asio::awaitable<void> run_session(asio::ip::tcp::socket sock, mysql::connection_pool& pool)
{
    // Read a request
    beast::flat_buffer buff;
    http::request<http::string_body> req;
    co_await http::async_read(sock, buff, req);

    // Parse the request
    std::int64_t employee_id = parse_request(req);

    // Query the database
    std::optional<std::string> company_name = co_await get_company(employee_id);

    // Compose the response
    http::response<http::string_body> res = compose_response(company_name);

    // Send the response
    co_await http::async_write(sock, res);
}

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

        // Launch a session
        asio::co_spawn(
            co_await asio::this_coro::executor,
            [socket = std::move(sock), &pool]() mutable { return run_session(std::move(socket), pool); },

            // TODO: change this by a logger
            [](std::exception_ptr ex) {
                if (ex)
                    std::rethrow_exception(ex);
            }
        );
    }
}

int main(int argc, char** argv)
{
    // Check command line arguments.
    if (argc != 5)
    {
        std::cerr << "Usage: " << argv[0] << " <db-username> <db-password> <db-hostname> <http-port>\n";
        return EXIT_FAILURE;
    }
    auto port = static_cast<unsigned short>(std::stoi(argv[4]));

    // Execution context
    asio::io_context ctx;

    // Launch the MySQL pool
    mysql::connection_pool pool(
        ctx,
        {
            .server_address = mysql::host_and_port(argv[3]),
            .username = argv[1],
            .password = argv[2],
            .database = "boost_mysql_examples",
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
