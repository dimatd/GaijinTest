#pragma once
#include "protocol.h"
#include <boost/asio.hpp>
#include <iostream>

namespace asio = boost::asio;
using boost::system::error_code;
using tcp = asio::ip::tcp;

template<class TDispatcher>
class t_connection : public i_socket, public std::enable_shared_from_this<t_connection<TDispatcher>>
{
using std::enable_shared_from_this<t_connection<TDispatcher>>::shared_from_this;
using t_connection_weak_ptr = std::weak_ptr<t_connection<TDispatcher>>;

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
		t_connection_weak_ptr self_weak = shared_from_this();

		idle_timer_.expires_after(std::chrono::seconds(30));

		idle_timer_.async_wait([self_weak](const error_code& ec) {
			auto self = self_weak.lock();
			if (!self) {
				return; // объект уже уничтожен
			}

			if (!ec) {
				std::cout << "Nothing happened for 30 seconds, closing connection\n";
				self->close();
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
		t_connection_weak_ptr self_weak = shared_from_this();
		
		auto data_ptr = std::make_shared<std::vector<uint8_t>>(cmd->serialize());
		asio::async_write(socket_, asio::buffer(*data_ptr),
			[data_ptr, self_weak](error_code ec, std::size_t /*length*/)
		{
			auto self = self_weak.lock();
			if(!self)
			{
				return;
			}

			if(ec)
			{
				std::cerr << "Send error: " << ec.message() << '\n';
				self->close();
			}
		});
	}

	void close()
	{
		if (!socket_.is_open())
		{
			return;
		}

		boost::system::error_code ec;
		socket_.cancel(ec);
		socket_.shutdown(tcp::socket::shutdown_both, ec);
		socket_.close(ec);
		idle_timer_.cancel();
	};

	template<class t_session_ptr>
	void do_read(const t_session_ptr& session)
	{
		if (!socket_.is_open())
		{
			return;
		}

		reset_idle_timer();
		t_connection_weak_ptr self_weak = shared_from_this();

		socket_.async_read_some(
			asio::buffer(buffer_.data() + leftover_, buffer_.size() - leftover_),
			[self_weak, session](error_code ec, std::size_t n)
		{
			auto self = self_weak.lock();
			if (!self)
			{
				return;
			}

			if(!ec)
			{
				constexpr size_t MSG_SIZE_BYTES = 4;
				size_t offset = 0;
				n += self->leftover_;

				while(n - offset >= MSG_SIZE_BYTES) {
					uint32_t msg_size = 0;
					std::memcpy(&msg_size, self->buffer_.data() + offset, MSG_SIZE_BYTES);

					if(n - offset < msg_size) {
						// Недостаточно данных для полного сообщения, ждём следующего чтения
						break;
					}

					std::vector<uint8_t> message(
						self->buffer_.begin() + offset + MSG_SIZE_BYTES,
						self->buffer_.begin() + offset + msg_size
					);

					read(message, self->dispatcher_, self->shared_from_this());

					offset += msg_size;
				}

				// После while-цикла:
				if(offset < n) {
					// Копируем остаток в начало буфера
					std::memmove(self->buffer_.data(), self->buffer_.data() + offset, n - offset);
					self->leftover_ = n - offset;
				}
				else {
					self->leftover_ = 0;
				}

				self->do_read(session); // читаем дальше
			}
			else if(ec != asio::error::eof)
			{
				std::cerr << "Read error: " << ec.message() << '\n';
				self->close();
			}
		});
	}

	inline tcp::socket& get_socket() { return socket_; }

private:
	#define BUFFER_SIZE  4*1024*1024 // 4 MB buffer size
  
	tcp::socket                           socket_;
	std::array<std::uint8_t, BUFFER_SIZE> buffer_{};
	TDispatcher&                          dispatcher_;
	std::size_t        	                  leftover_ = 0;
	asio::steady_timer                    idle_timer_;
};
