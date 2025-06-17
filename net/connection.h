#pragma once
#include "protocol.h"
#include <boost/asio.hpp>
#include <iostream>
#include <queue>

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
		, io_(io)
		, strand_(asio::make_strand(io.get_executor()))
	{}

	~t_connection() override
	{
		std::cout << "Connection closed\n";
	}

	void reset_idle_timer() {
		t_connection_weak_ptr self_weak = shared_from_this();

		idle_timer_.cancel();
		idle_timer_.expires_after(std::chrono::seconds(30));

		idle_timer_.async_wait([self_weak](const error_code& ec) {
			auto self = self_weak.lock();
			if (!self) {
				return; // объект уже уничтожен
			}

			if (ec == asio::error::operation_aborted) {
				return; // таймер был отменён, ничего не делаем
			}

			if (!ec) {
				std::cout << "Nothing happened for 30 seconds, closing connection\n";
				asio::post(self->strand_, [self]() { self->close(); });;
			}
		});
	}
	
	void send(const base_command_ptr& cmd) override
	{
		t_connection_weak_ptr self_weak = shared_from_this();

		asio::post(strand_, [self_weak, cmd]() {
			auto self = self_weak.lock();
			if(!self)
			{
				return;
			}

			if(!self->socket_.is_open())
			{
				return;
			}

			self->reset_idle_timer();

			bool write_in_progress = !self->send_queue_.empty();
			self->send_queue_.push(cmd->serialize().get_buffer());
			if(!write_in_progress) {
				self->do_write();
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
		t_connection_weak_ptr self_weak = shared_from_this();

		asio::post(strand_, [self_weak, session]()
		{
			auto self = self_weak.lock();
			if(!self)
			{
				return;
			}

			if(!self->socket_.is_open())
			{
				return;
			}

			self->reset_idle_timer();

			self->socket_.async_read_some(
				asio::buffer(self->buffer_.data() + self->leftover_, self->buffer_.size() - self->leftover_),
				[self_weak, session](error_code ec, std::size_t n)
			{
				auto self = self_weak.lock();
				if(!self)
				{
					return;
				}

				if(ec == asio::error::operation_aborted)
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

						try
						{
							read(message, self->dispatcher_, self->shared_from_this());
						}
						catch(const std::exception& e)
						{
							std::cerr << "Read Error: " << e.what() << std::endl;
							asio::post(self->strand_, [self]() { self->close(); });
							return;
						}

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
					asio::post(self->strand_, [self]() { self->close(); });
					return;
				}
			});

		});
	}

	void do_write() {
		auto data = send_queue_.front();
		auto data_ptr = std::make_shared<std::vector<uint8_t>>(std::move(data));

		t_connection_weak_ptr self_weak = shared_from_this();

		asio::async_write(socket_, asio::buffer(*data_ptr),
			[self_weak, data_ptr](error_code ec, std::size_t /*length*/)
			{
				auto self = self_weak.lock();
				if(!self)
				{
					return;
				}

				if(ec == asio::error::operation_aborted)
				{
					return;
				}

				if(ec)
				{
					std::cout << "closing connection ec: " << ec.message() << '\n';
					asio::post(self->strand_, [self]() { self->close(); });
					return;
				}

				self->send_queue_.pop();
				if(!self->send_queue_.empty()) {
					self->do_write(); // отправляем следующий
				}
			});
	}

	inline tcp::socket& get_socket() { return socket_; }

private:
	#define BUFFER_SIZE  4*1024*1024 // 4 MB buffer size
  
	std::queue<std::vector<uint8_t>>              send_queue_;
	tcp::socket                                   socket_;
	std::array<std::uint8_t, BUFFER_SIZE>         buffer_{};
	TDispatcher&                                  dispatcher_;
	std::size_t        	                          leftover_ = 0;
	asio::steady_timer                            idle_timer_;
	asio::io_context&                             io_;
	asio::strand<asio::io_context::executor_type> strand_;
};
