#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

enum class ecommand_type : std::uint8_t
{
	GET,
	SET,
	GET_RESPONSE,
};

class base_command
{
public:
	virtual ~base_command() = default;

	inline base_command(ecommand_type type)
		: type(type) {}

	base_command(base_command&& other) = delete;
	base_command& operator=(base_command&& other) = delete;
	base_command(const base_command&) = delete;
	base_command& operator=(const base_command&) = delete;

	virtual std::vector<uint8_t> serialize          () const;
	virtual size_t               get_serialized_size() const;

	virtual void read(std::span<const uint8_t>& view);

private:
	ecommand_type type;
};

class command : public base_command
{
public:
	inline command(ecommand_type type, const std::string& key)
		: base_command(type), key(key) {}

	inline command(ecommand_type type, std::string&& key)
		: base_command(type), key(std::move(key)) {}

	inline command(ecommand_type type)
		: base_command(type) {}

	virtual std::vector<uint8_t> serialize() const;
	virtual size_t               get_serialized_size() const;

	virtual void read(std::span<const uint8_t>& view);

	inline const std::string& get_key() const { return key; }

private:
	std::string key;
};

class get_command : public command
{
public:
	inline get_command(const std::string& key)
		: command(ecommand_type::GET, key) {}

	inline get_command(std::string&& key)
		: command(ecommand_type::GET, std::move(key)) {}

	inline get_command()
		: command(ecommand_type::GET) {}

	std::vector<uint8_t> serialize          () const override;
	size_t               get_serialized_size() const override;

	void read(std::span<const uint8_t>& view) override;

private:
	uint16_t request_id = get_command::next_request_id();
	static uint16_t next_request_id();

	friend class get_command_response;
};

class set_command : public command
{
public:
	inline set_command(const std::string& key, const std::string& value)
		: command(ecommand_type::SET, key), value(value) {}

	inline set_command(std::string&& key, std::string&& value)
		: command(ecommand_type::SET, std::move(key)), value(std::move(value)) {}

	inline set_command()
		: command(ecommand_type::SET) {}

	std::vector<uint8_t> serialize          () const override;
	size_t               get_serialized_size() const override;

	void read(std::span<const uint8_t>& view) override;

	inline const std::string& get_value() const { return value; }

private:
	std::string value;
};

using base_command_ptr = std::shared_ptr<base_command>;
using get_command_ptr = std::shared_ptr<get_command>;
using set_command_ptr = std::shared_ptr<set_command>;

class get_command_response : public command
{
public:
	inline get_command_response(const get_command_ptr& cmd, const std::string& value, uint64_t reads, uint64_t writes)
		: command(ecommand_type::GET_RESPONSE, cmd->get_key()), request_id(cmd->request_id)
		, value(value), reads(reads), writes(writes)
	{}

	inline get_command_response() : command(ecommand_type::GET_RESPONSE) {}

	std::vector<uint8_t> serialize          () const override;
	size_t               get_serialized_size() const override;

	void read(std::span<const uint8_t>& view) override;

	inline uint16_t           get_request_id() const { return request_id; }
	inline const std::string& get_value     () const { return value; }
	inline uint64_t           get_reads     () const { return reads; }
	inline uint64_t           get_writes    () const { return writes; }
	
private:
	uint16_t request_id = 0;
	uint64_t reads      = 0;
	uint64_t writes     = 0;
		
	std::string value;
};

using get_command_response_ptr = std::shared_ptr<get_command_response>;

class i_socket
{
public:
	virtual ~i_socket() = default;
	
	virtual void send(const base_command_ptr& cmd) = 0;
};

using i_socket_ptr = std::shared_ptr<i_socket>;

class i_server_dispatcher
{
public:
	virtual ~i_server_dispatcher() = default;
	
	virtual void process(const get_command_ptr& cmd, const i_socket_ptr& socket) = 0;
	virtual void process(const set_command_ptr& cmd, const i_socket_ptr& socket) = 0;
};

class i_client_dispatcher
{
public:
	virtual ~i_client_dispatcher() = default;
	
	virtual void process(const get_command_response_ptr& cmd, const i_socket_ptr& socket) = 0;
};

void read(const std::vector<uint8_t>& buf, i_server_dispatcher& dispatcher, const i_socket_ptr& socket);
void read(const std::vector<uint8_t>& buf, i_client_dispatcher& dispatcher, const i_socket_ptr& socket);