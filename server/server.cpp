#include <boost/asio.hpp>
#include <iostream>
#include <array>
#include <memory>
#include <thread>

#include "config_store.h"
#include "server_dispatcher.h"
#include <connection.h>

class config_store;
namespace asio = boost::asio;
using boost::system::error_code;
using tcp = asio::ip::tcp;

using connection = t_connection<server_dispatcher>;
using connection_ptr = std::shared_ptr<connection>;

// -----------------------------------------------------------------------------
// Одна клиентская сессия
// -----------------------------------------------------------------------------
class session : public std::enable_shared_from_this<session>
{
public:
	explicit session(asio::io_context& io, tcp::socket sock, config_store& store)
		: dispatcher_(store), conn_(std::make_shared<connection>(io, std::move(sock), dispatcher_))
	{}

	~session()
	{
		std::cout << "Session closed\n";
	}

	void start() { conn_->do_read(); }

private:
	server_dispatcher dispatcher_;
	connection_ptr    conn_;
};

// -----------------------------------------------------------------------------
// Аcceptor
// -----------------------------------------------------------------------------
class server
{
public:
	server(asio::io_context& io, std::uint16_t port, config_store& store)
		: acceptor_(io, tcp::endpoint(tcp::v4(), port))
		, store(store)
		, save_timer_(io)
		, stat_timer_(io)
		, io(io)
	{
		std::cout << "Server started on port " << port << '\n';
		do_accept();
		start_save_timer(); // Запускаем таймер для периодического сохранения
		start_stat_timer(); // Запускаем таймер для периодической печати статистики
	}

private:
	void save_store()
	{
		if(store.flush_if_dirty())
			std::cout << "Store saved to disk.\n";
	}

	void print_stat()
	{
		auto& stats = store.get_stats();
		stats.dump_and_reset();
	}
	
	void start_stat_timer()
	{
		stat_timer_.expires_after(std::chrono::seconds(5));
		stat_timer_.async_wait([this](const error_code& ec) {
			if(!ec) {
				print_stat();
				start_stat_timer(); // перезапуск таймера
			}
		});
	}

	void start_save_timer()
	{
		save_timer_.expires_after(std::chrono::seconds(10));
		save_timer_.async_wait([this](const error_code& ec) {
			if(!ec) {
				save_store();
				start_save_timer(); // перезапуск таймера
			}
		});
	}
	
	void do_accept()
	{
		acceptor_.async_accept(
			[this](error_code ec, tcp::socket socket)
		{
			if(!ec)
				std::make_shared<session>(io, std::move(socket), store)->start();
			else
				std::cerr << "Accept error: " << ec.message() << '\n';

			do_accept(); // ждём следующий коннект
		});
	}

	tcp::acceptor      acceptor_;
	config_store&      store;
	asio::steady_timer save_timer_;
	asio::steady_timer stat_timer_;
	asio::io_context&  io;
};

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------
int main()
{
	try
	{
		asio::io_context io;
		config_store store("config.dat"); // Путь к файлу конфигурации
		server srv(io, 9000, store);

		// ───── Выбираем модель параллелизма ─────
		//const std::size_t threads = 1;          // ← 1 = классическая однопоточная «async I/O»
		const std::size_t threads = std::thread::hardware_concurrency(); // ← включить пул

		std::vector<std::thread> pool;
		for(std::size_t i = 0; i < threads; ++i)
			pool.emplace_back([&io] { io.run(); });

		for(auto& t : pool) t.join();
	}
	catch(const std::exception& e)
	{
		std::cerr << "Fatal: " << e.what() << '\n';
	}
}
