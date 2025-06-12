#pragma once

#include <protocol.h>

class config_store;

class server_dispatcher : public i_server_dispatcher
{
public:
	inline server_dispatcher(config_store& store)
		: store_(store) {}
	
	void process(const get_command_ptr& cmd, i_socket& socket) override;
	void process(const set_command_ptr& cmd, i_socket& socket) override;
private:
	config_store& store_;
};
