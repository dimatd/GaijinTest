#include "server_dispatcher.h"

#include "config_store.h"

void server_dispatcher::process(const get_command_ptr& cmd, i_socket& socket)
{
	auto opt = store_.get(cmd->get_key());
	uint64_t reads = 0;
	uint64_t writes = 0;
	std::string value = "not found";

	if(opt) {
		const auto& [key, entry] = *opt;
		reads = entry->reads.load();
		writes = entry->writes.load();
		value = entry->value;
	}

	get_command_response_ptr response = make_shared<get_command_response>(cmd, value, reads, writes);
	socket.send(response);
}

void server_dispatcher::process(const set_command_ptr& cmd, i_socket& socket)
{
	store_.set(cmd->get_key(), cmd->get_value());
}
