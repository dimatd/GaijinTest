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
	explicit t_connection(tcp::socket sock, TDispatcher& dispatcher)
		: socket_(std::move(sock))
		, dispatcher_(dispatcher)
	{}

	void send(const base_command_ptr& cmd) override
	{
		auto data_ptr = std::make_shared<std::vector<uint8_t>>(cmd->serialize());
		asio::async_write(socket_, asio::buffer(*data_ptr),
			[data_ptr](error_code ec, std::size_t /*length*/)
		{
			if(ec)
				std::cerr << "Send error: " << ec.message() << '\n';
		});

		//std::cout << "Sent command: " << '\n';
	}

	template<class t_session_ptr>
	void do_read(const t_session_ptr& session)
	{
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
	tcp::socket                    socket_;
	std::array<std::uint8_t, 4096> buffer_{};
	TDispatcher&                   dispatcher_;
	std::size_t        	           leftover_ = 0;
};
