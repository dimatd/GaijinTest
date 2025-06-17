#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <protocol.h>
#include <connection.h>

#include <random>

namespace asio = boost::asio;
using boost::system::error_code;
using tcp = asio::ip::tcp;
using namespace std::chrono_literals;

class client_dispatcher : public i_client_dispatcher
{
public:
	void process(const get_command_response_ptr& cmd, const i_socket_ptr& socket) override
	{
		static int count = 0;
		if (++count % 1000 == 0) {
			std::cout << "Processed " << count << " get responses\n";
		
			std::cout << "Received response for key: " << cmd->get_key() << std::endl
				<< ", value: " << cmd->get_value() << std::endl
				<< ", reads: " << cmd->get_reads() << std::endl
				<< ", writes: " << cmd->get_writes() << std::endl;
		}
	}
};

using connection = t_connection<client_dispatcher>;
using connection_ptr = std::shared_ptr<connection>;

class spammer
{
public:
	spammer(asio::io_context& io, const tcp::resolver::results_type& endpoints)
		: conn_(make_shared<connection>(io, tcp::socket(io), dispatcher_))
	{
		asio::async_connect(conn_->get_socket(), endpoints,
			[this](error_code ec, const tcp::endpoint&)
		{
			if(!ec) {
				std::cout << "Connected to server\n";
				start_send_loop();
			}
			else {
				std::cerr << "Connect error: " << ec.message() << '\n';
			}
		});
	}

private:
	void start_send_loop()
	{
		conn_->read(std::shared_ptr<spammer>());

		/*for(int i = 0; i < 1000; ++i)
		{
			conn_.send(generate_set_command());
		}*/
		
		for (int i = 0; i < 1000000; ++i)
		{
			send_once();
		}
	}

	std::string generate_test(const std::string& start_string) {
		std::mt19937 rng{std::random_device{}()};
		std::uniform_int_distribution<> dist(1, 100);
		int n = dist(rng);
		return start_string + std::to_string(n);
	}
	
	std::string generate_test_key() {
		return generate_test("testKey");
	}

	std::string generate_test_value() {
		return generate_test("testValue");
	}

	base_command_ptr generate_set_command()
	{
		std::string key = generate_test_key();
		all_keys_.push_back(key);

		std::string value = generate_test_value();
		return std::make_shared<set_command>(std::move(key), std::move(value));
	}

	base_command_ptr generate_get_command()
	{
		if(all_keys_.empty()) {
			return std::make_shared<get_command>(generate_test_key());
		}
		else {
			auto index = std::rand() % all_keys_.size();
			const auto& key = all_keys_[index];
			return std::make_shared<get_command>(key);
		}
	}

	base_command_ptr generate_command()
	{
		bool is_set = (std::rand() % 100) == 0;
		return is_set ? generate_set_command() : generate_get_command();
	}

	void send_once()
	{
		conn_->send(generate_command());
	}

	client_dispatcher        dispatcher_;
	connection_ptr           conn_;
	std::vector<std::string> all_keys_;
};

int main()
{
	try {
		asio::io_context io;

		tcp::resolver resolver(io);
		auto endpoints = resolver.resolve("127.0.0.1", "9000");

		spammer client(io, endpoints);

		io.run();
	}
	catch(const std::exception& e) {
		std::cerr << "Fatal: " << e.what() << '\n';
	}

	std::cout << "Client finished\n";
}