#pragma once
#include "protocol.h"
#include <boost/asio.hpp>
#include <iostream>

namespace asio = boost::asio;
using boost::system::error_code;
using tcp = asio::ip::tcp;

template<class TDispatcher>
class t_connection : public i_socket
{
public:
	explicit t_connection(asio::io_context& io, tcp::socket sock, TDispatcher& dispatcher)
		: socket_(std::move(sock))
		, dispatcher_(dispatcher)
		, idle_timer_(io)
	{}

	~t_connection() override
	{
		std::cout << "Connection closed\n";
	}

	void reset_idle_timer() {
		idle_timer_.expires_after(std::chrono::seconds(30));
		idle_timer_.async_wait([this](const error_code& ec) {
			if (!ec) {
				std::cout << "Нет активности, отключаемся\n";
				socket_.shutdown(tcp::socket::shutdown_both);
			}
		});
	}
	
	void send(const base_command_ptr& cmd) override
	{
		if (!socket_.is_open())
		{
			return;
		}

		reset_idle_timer();
		
		auto data_ptr = std::make_shared<std::vector<uint8_t>>(cmd->serialize());
		asio::async_write(socket_, asio::buffer(*data_ptr),
				  [data_ptr, this](error_code ec, std::size_t /*length*/)
		{
		  if(ec){
				std::cerr << "Send error: " << ec.message() << '\n';
				boost::system::error_code ignored_ec;
				socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
				socket_.close(ignored_ec);
		  }
		});
	}

	template<class t_session_ptr>
	void do_read(const t_session_ptr& session)
	{
		if (!socket_.is_open())
		{
			return;
		}

		reset_idle_timer();
		
		socket_.async_read_some(
			asio::buffer(buffer_.data() + leftover_, buffer_.size() - leftover_),
			[this, session](error_code ec, std::size_t n)
		{
			if(!ec)
			{
				constexpr size_t MSG_SIZE_BYTES = 4;
				size_t offset = 0;
				n += leftover_;

				while(n - offset >= MSG_SIZE_BYTES) {
					uint32_t msg_size = 0;
					std::memcpy(&msg_size, buffer_.data() + offset, MSG_SIZE_BYTES);

					if(n - offset < msg_size) {
						// Недостаточно данных для полного сообщения, ждём следующего чтения
						break;
					}

					std::vector<uint8_t> message(
						buffer_.begin() + offset + MSG_SIZE_BYTES,
						buffer_.begin() + offset + msg_size
					);

					read(message, dispatcher_, *this);
					// TODO: обработка message

					offset += msg_size;
				}

				// После while-цикла:
				if(offset < n) {
					// Копируем остаток в начало буфера
					std::memmove(buffer_.data(), buffer_.data() + offset, n - offset);
					leftover_ = n - offset;
				}
				else {
					leftover_ = 0;
				}

				//std::cout << "Received " << n << " bytes from "
				//	<< socket_.remote_endpoint() << '\n';
				// TODO: обработка данных в buffer_.data(), длиной n.
				do_read(session); // читаем дальше
			}
			else if(ec != asio::error::eof)
			{
				std::cerr << "Read error: " << ec.message() << '\n';
			}
		});
	}

	inline tcp::socket& get_socket() { return socket_; }

private:
        #define BUFFER_SIZE  4*1024*1024
  
	tcp::socket                           socket_;
	std::array<std::uint8_t, BUFFER_SIZE> buffer_{};
	TDispatcher&                          dispatcher_;
	std::size_t        	              leftover_ = 0;
	asio::steady_timer                    idle_timer_;
};
