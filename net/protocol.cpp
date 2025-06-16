
#include "protocol.h"

#include <cstdint>
#include <vector>
#include <string>
#include <span>
#include <stdexcept>
#include <format>

// --- запись целого в little-endian ---
template <class T>
void write_le(std::vector<uint8_t>& buf, T value)
{
	static_assert(std::is_integral_v<T>);
	for(size_t i = 0; i < sizeof(T); ++i)
		buf.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

void write(const std::string& value, std::vector<uint8_t>& buf)
{
	auto len = static_cast<uint32_t>(value.size());
	write_le<uint32_t>(buf, len);
	buf.insert(buf.end(), value.begin(), value.end());
};

// --- чтение целого из little-endian ---
template <class T>
T read_le(std::span<const uint8_t>& view)
{
	static_assert(std::is_integral_v<T>);
	if(view.size() < sizeof(T))
		throw std::runtime_error(
			std::format("truncated buffer: view.size() = {}, expected >= {}", view.size(), sizeof(T))
		);

	T value = 0;
	for(size_t i = 0; i < sizeof(T); ++i)
		value |= static_cast<T>(view[i]) << (i * 8);

	view = view.subspan(sizeof(T));   // «сдвигаем» курсор
	return value;
}

void read(std::string& value, std::span<const uint8_t>& view)
{
	auto len = read_le<uint32_t>(view);
	if(len > view.size())
		throw std::runtime_error(
			std::format("truncated buffer: view.size() = {}, expected >= {}", view.size(), len)
		);

	value.assign(reinterpret_cast<const char*>(view.data()), len);
	view = view.subspan(len);  // «сдвигаем» курсор
}

//-- base_command

std::vector<uint8_t> base_command::serialize() const
{
	std::vector<uint8_t> buf;
	uint32_t size = (uint32_t)get_serialized_size();
	//buf.reserve(size);

	write_le<uint32_t>(buf, size);
	write_le<uint8_t>(buf, static_cast<uint8_t>(type));

	return buf;
}

size_t base_command::get_serialized_size() const
{
	return sizeof(uint32_t) + sizeof(uint8_t);
}

void base_command::read(std::span<const uint8_t>& view)
{
	type = static_cast<ecommand_type>(read_le<uint8_t>(view));
}

//-- command

std::vector<uint8_t> command::serialize() const
{
	std::vector<uint8_t> buf{ base_command::serialize() };
	write(key, buf);
	return buf;
}

size_t command::get_serialized_size() const
{
	return base_command::get_serialized_size() + sizeof(uint32_t) + key.size();
}

void command::read(std::span<const uint8_t>& view)
{
	::read(key, view);
}

//-- set_command

std::vector<uint8_t> set_command::serialize() const
{
	std::vector<uint8_t> result{ command::serialize() };
	write(value, result);

	return result;
}

void set_command::read(std::span<const uint8_t>& view)
{
	command::read(view);

	::read(value, view);
}

size_t set_command::get_serialized_size() const
{
	return command::get_serialized_size() + sizeof(uint32_t) + value.size();
}

//-- get_command

std::vector<uint8_t> get_command::serialize() const
{
	std::vector<uint8_t> result{ command::serialize() };
	write_le<uint16_t>(result, request_id);

	return result;
}

size_t get_command::get_serialized_size() const
{
	return command::get_serialized_size() + sizeof(uint16_t);
}

void get_command::read(std::span<const uint8_t>& view)
{
	command::read(view);

	if(view.size() < sizeof(uint16_t))
		throw std::runtime_error(
			std::format("truncated buffer: view.size() = {}, expected >= {}", view.size(), sizeof(uint16_t))
		);

	request_id = read_le<uint16_t>(view);
	if(request_id == 0)
		throw std::runtime_error("request_id cannot be zero");
}

//-- get_command_response

std::vector<uint8_t> get_command_response::serialize() const
{
	std::vector<uint8_t> buff = command::serialize();
	write_le<uint16_t>(buff, request_id);
	write_le<uint64_t>(buff, reads);
	write_le<uint64_t>(buff, writes);
	write(value, buff);

	return buff;
}

size_t get_command_response::get_serialized_size() const
{
	return command::get_serialized_size()
		+ sizeof(request_id) + sizeof(reads)
		+ sizeof(writes) + sizeof(uint32_t) + value.size();
}

void get_command_response::read(std::span<const uint8_t>& view)
{
	command::read(view);

	request_id = read_le<uint16_t>(view);
	reads      = read_le<uint64_t>(view);
	writes     = read_le<uint64_t>(view);
	::read(value, view);

	if(request_id == 0)
		throw std::runtime_error("request_id cannot be zero");
}

template<class T, class TDispatcher>
inline void process(std::span<const uint8_t>& view, TDispatcher& dispatcher, const i_socket_ptr& socket)
{
	std::shared_ptr<T> result = std::make_shared<T>();
	result->read(view);

	if(!view.empty())
		throw std::runtime_error(
			std::format("truncated buffer: view.size() = {}, !view.empty()", view.size())
		);;

	dispatcher.process(result, socket);
}

void read(const std::vector<uint8_t>& buf, i_server_dispatcher& dispatcher, const i_socket_ptr& socket)
{
	std::span<const uint8_t> view{ buf };
	const auto type_raw = static_cast<ecommand_type>(read_le<uint8_t>(view));

	switch(type_raw)
	{
	case ecommand_type::GET:
		process<get_command>(view, dispatcher, socket);
		break;
	case ecommand_type::SET:
		process<set_command>(view, dispatcher, socket);
		break;
	default:
		throw std::runtime_error("unknown command type");
	}
}

void read(const std::vector<uint8_t>& buf, i_client_dispatcher& dispatcher, const i_socket_ptr& socket)
{
	std::span<const uint8_t> view{ buf };
	const auto type_raw = static_cast<ecommand_type>(read_le<uint8_t>(view));

	if (type_raw != ecommand_type::GET_RESPONSE)
		throw std::runtime_error("expected GET_RESPONSE command");

	process<get_command_response>(view, dispatcher, socket);
}

uint16_t get_command::next_request_id()
{
	static uint16_t id = 0;
	++id;
	if(id == 0) // Если id обнулился, начинаем с 1
		id = 1;
	return id;
}


