﻿#pragma once
#include "protocol.h"
#include <boost/asio.hpp>
#include <iostream>
#include <queue>
#include <cstring>
#include <limits>

namespace asio = boost::asio;
using boost::system::error_code;
using tcp = asio::ip::tcp;

constexpr size_t MAX_MESSAGE_SIZE = 1024 * 1024; // 1 MB
constexpr size_t MSG_SIZE_BYTES = 4;
constexpr size_t BUFFER_SIZE = 4 * 1024 * 1024; // 4 MB

template<class t_dispatcher>
class t_connection : public i_socket, public std::enable_shared_from_this<t_connection<t_dispatcher>>
{
	using std::enable_shared_from_this<t_connection<t_dispatcher>>::shared_from_this;
	using t_connection_weak_ptr = std::weak_ptr<t_connection<t_dispatcher>>;

public:
	explicit t_connection(asio::io_context& io, tcp::socket sock, t_dispatcher& dispatcher)
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

	void send(const base_command_ptr& cmd) override
	{
		t_connection_weak_ptr self_weak = shared_from_this();

		asio::post(strand_, [self_weak, cmd]() {
			auto self = self_weak.lock();
			if(!self || !self->socket_.is_open()) return;

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
		if(!socket_.is_open()) return;

		boost::system::error_code ec;
		socket_.cancel(ec);
		socket_.shutdown(tcp::socket::shutdown_both, ec);
		socket_.close(ec);
		idle_timer_.cancel();

		send_queue_ = {};
	}

	inline tcp::socket& get_socket() { return socket_; }

	template<class t_session_ptr>
	void read(const t_session_ptr& session)
	{
		//if(!session) return;

		t_connection_weak_ptr self_weak = shared_from_this();

		asio::post(strand_, [self_weak, session]() {
			auto self = self_weak.lock();
			if(!self) return;

			self->template do_read<t_session_ptr>(session);
		});
	}

private:
	void reset_idle_timer() {
		t_connection_weak_ptr self_weak = shared_from_this();

		idle_timer_.cancel();
		idle_timer_.expires_after(std::chrono::seconds(30));

		idle_timer_.async_wait([self_weak](const error_code& ec) {
			auto self = self_weak.lock();
			if(!self || ec == asio::error::operation_aborted) return;

			if(!ec) {
				std::cout << "Nothing happened for 30 seconds, closing connection\n";
				asio::post(self->strand_, [self]() { self->close(); });
			}
		});
	}

	template<class t_session_ptr>
	void do_read(const t_session_ptr& session)
	{
		if(!socket_.is_open()) return;

		reset_idle_timer();
		t_connection_weak_ptr self_weak = shared_from_this();

		socket_.async_read_some(
			asio::buffer(buffer_.data() + unparsed_bytes_, buffer_.size() - unparsed_bytes_),
			[self_weak, session](error_code ec, std::size_t n)
		{
			auto self = self_weak.lock();
			if(!self || ec == asio::error::operation_aborted) return;

			if(!ec)
			{
				size_t offset = 0;
				n += self->unparsed_bytes_;

				while(n - offset >= MSG_SIZE_BYTES) {
					uint32_t msg_size = 0;
					std::memcpy(&msg_size, self->buffer_.data() + offset, MSG_SIZE_BYTES);

					if(msg_size < MSG_SIZE_BYTES || msg_size > MAX_MESSAGE_SIZE) {
						std::cerr << "Invalid message size: " << msg_size << std::endl;
						asio::post(self->strand_, [self]() { self->close(); });
						return;
					}

					if(n - offset < msg_size) break;

					std::vector<uint8_t> message(
						self->buffer_.begin() + offset + MSG_SIZE_BYTES,
						self->buffer_.begin() + offset + msg_size
					);

					try {
						::read(message, self->dispatcher_, self);
					}
					catch(const std::exception& e) {
						std::cerr << "Read Error: " << e.what() << std::endl;
						asio::post(self->strand_, [self]() { self->close(); });
						return;
					}

					offset += msg_size;
				}

				if(offset < n) {
					std::memmove(self->buffer_.data(), self->buffer_.data() + offset, n - offset);
					self->unparsed_bytes_ = n - offset;
				}
				else {
					self->unparsed_bytes_ = 0;
				}

				self->template do_read<t_session_ptr>(session);
			}
			else if(ec != asio::error::eof)
			{
				std::cerr << "Read error: " << ec.message() << '\n';
				asio::post(self->strand_, [self]() { self->close(); });
			}
		});
	}

	void send_next()
	{
		send_queue_.pop();
		if(!send_queue_.empty()) {
			do_write();
		}
	}

	void do_write() {
		auto data = send_queue_.front();
		auto data_ptr = std::make_shared<std::vector<uint8_t>>(std::move(data));

		t_connection_weak_ptr self_weak = shared_from_this();
		reset_idle_timer();

		asio::async_write(socket_, asio::buffer(*data_ptr),
			[self_weak, data_ptr](error_code ec, std::size_t /*length*/)
		{
			auto self = self_weak.lock();
			if(!self) return;

			if(ec == asio::error::operation_aborted) return;

			if(ec)
			{
				std::cerr << "closing connection ec: " << ec.message() << '\n';
				asio::post(self->strand_, [self]() { self->close(); });
				return;
			}

			asio::post(self->strand_, [self]() { self->send_next(); });
		});
	}

private:
	std::queue<std::vector<uint8_t>>              send_queue_;
	tcp::socket                                   socket_;
	std::array<std::uint8_t, BUFFER_SIZE>         buffer_{};
	t_dispatcher&                                 dispatcher_;
	std::size_t                                   unparsed_bytes_ = 0;
	asio::steady_timer                            idle_timer_;
	asio::io_context&                             io_;
	asio::strand<asio::io_context::executor_type> strand_;
};
