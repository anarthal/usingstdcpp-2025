
#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/with_params.hpp>

#include <boost/asio/awaitable.hpp>
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

    return res;
}

asio::awaitable<std::optional<std::string>> get_company(
    mysql::connection_pool& pool,
    std::int64_t employee_id
)
{
    // Get a connection from the pool
    mysql::pooled_connection conn = co_await pool.async_get_connection(
        asio::cancel_after(std::chrono::seconds(20))
    );

    // Query the database
    mysql::results result;
    co_await conn->async_execute(
        mysql::with_params("SELECT company_id FROM employee WHERE id = {}", employee_id),
        result
    );

    // Compose the message to be sent back to the client
    if (result.rows().empty())
    {
        co_return std::nullopt;
    }
    else
    {
        co_return result.rows().at(0).at(0).as_string();
    }
}

asio::awaitable<http::response<http::string_body>> handle_request(
    mysql::connection_pool& pool,
    const http::request<http::empty_body>& req
)
{
    http::response<http::string_body> res;

    // Parse the request
    std::optional<std::int64_t> employee_id = parse_request(req);
    if (!employee_id)
    {
        res.result(http::status::bad_request);
        co_return res;
    }

    // Query the database
    std::optional<std::string> company_name = co_await get_company(pool, *employee_id);
    if (!company_name)
    {
        res.result(http::status::not_found);
        co_return res;
    }

    // Compose the response
    res.body() = *company_name;
    co_return res;
}

static asio::awaitable<void> run_session(mysql::connection_pool& pool, asio::ip::tcp::socket sock)
{
    // Read a request
    beast::flat_buffer buff;
    http::request<http::empty_body> req;
    co_await http::async_read(sock, buff, req);

    // Handle it
    http::response<http::string_body> res = co_await handle_request(pool, req);

    // Send the response
    res.version(req.version());
    res.keep_alive(false);
    res.prepare_payload();
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
            [socket = std::move(sock), &pool]() mutable { return run_session(pool, std::move(socket)); },

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
