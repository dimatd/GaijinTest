
#include "protocol.h"

#include <cstdint>
#include <vector>
#include <string>
#include <span>
#include <stdexcept>
#include <format>
#include <cassert>

//-- base_command

memory_writer base_command::serialize() const
{
	memory_writer writer;
	uint32_t size = (uint32_t)get_serialized_size();

	writer.write(size);
	writer.write((uint8_t)type);

	return writer;
}

size_t base_command::get_serialized_size() const
{
	return sizeof(uint32_t) + sizeof(uint8_t);
}

void base_command::read(memory_reader& reader)
{
	uint8_t type_raw;
	reader.read(type_raw);

	type_raw = static_cast<uint8_t>(type_raw);
}

//-- command

memory_writer command::serialize() const
{
	memory_writer writer{ base_command::serialize() };
	writer.write(key);
	return writer;
}

size_t command::get_serialized_size() const
{
	return base_command::get_serialized_size() + sizeof(uint32_t) + key.size();
}

void command::read(memory_reader& reader)
{
	reader.read(key);
}

//-- set_command

memory_writer set_command::serialize() const
{
	memory_writer writer{ command::serialize() };
	writer.write(value);

	return writer;
}

void set_command::read(memory_reader& reader)
{
	command::read(reader);

	reader.read(value);
}

size_t set_command::get_serialized_size() const
{
	return command::get_serialized_size() + sizeof(uint32_t) + value.size();
}

//-- get_command

memory_writer get_command::serialize() const
{
	memory_writer writer{ command::serialize() };
	writer.write(request_id);

	return writer;
}

size_t get_command::get_serialized_size() const
{
	return command::get_serialized_size() + sizeof(uint16_t);
}

void get_command::read(memory_reader& reader)
{
	command::read(reader);

	reader.read(request_id);
	if(request_id == 0)
		throw std::runtime_error("request_id cannot be zero");
}

//-- get_command_response

memory_writer get_command_response::serialize() const
{
	memory_writer writer = command::serialize();

	writer.write(request_id);
	writer.write(reads);
	writer.write(writes);
	writer.write(value);

	return writer;
}

size_t get_command_response::get_serialized_size() const
{
	return command::get_serialized_size()
		+ sizeof(request_id) + sizeof(reads)
		+ sizeof(writes) + sizeof(uint32_t) + value.size();
}

void get_command_response::read(memory_reader& reader)
{
	command::read(reader);
	
	reader.read(request_id);
	reader.read(reads);
	reader.read(writes);

	reader.read(value);

	if(request_id == 0)
		throw std::runtime_error("request_id cannot be zero");
}

template<class T, class TDispatcher>
inline void process(memory_reader& reader, TDispatcher& dispatcher, const i_socket_ptr& socket)
{
	std::shared_ptr<T> result = std::make_shared<T>();
	result->read(reader);

	if(!reader.is_end())
		throw std::runtime_error(
			std::format("truncated buffer: reader.size() = {}, !reader.is_end()", reader.size())
		);;

	dispatcher.process(result, socket);
}

void read(const std::vector<uint8_t>& buf, i_server_dispatcher& dispatcher, const i_socket_ptr& socket)
{
	memory_reader reader{ buf };
	uint8_t type_raw;
	reader.read(type_raw);

	const auto type = static_cast<ecommand_type>(type_raw);


	switch(type)
	{
	case ecommand_type::GET:
		process<get_command>(reader, dispatcher, socket);
		break;
	case ecommand_type::SET:
		process<set_command>(reader, dispatcher, socket);
		break;
	default:
		throw std::runtime_error("unknown command type");
	}
}

void read(const std::vector<uint8_t>& buf, i_client_dispatcher& dispatcher, const i_socket_ptr& socket)
{
	memory_reader reader{ buf };
	uint8_t type_raw;
	reader.read(type_raw);

	const auto type = static_cast<ecommand_type>(type_raw);

	if (type != ecommand_type::GET_RESPONSE)
		throw std::runtime_error("expected GET_RESPONSE command");

	process<get_command_response>(reader, dispatcher, socket);
}

uint16_t get_command::next_request_id()
{
	static uint16_t id = 0;
	++id;
	if(id == 0) // Если id обнулился, начинаем с 1
		id = 1;
	return id;
}


