#pragma once

#include <immer/map.hpp>      // persistent RB-tree
#include <immer/atom.hpp>     // lock-free атом с CAS
#include <atomic>
#include <filesystem>
#include <string>
#include <optional>

struct entry {
	std::string value;
	std::atomic<uint64_t> reads{ 0 }, writes{ 0 };
};

using entry_ptr = std::shared_ptr<entry>;
using map = immer::map<std::string, entry_ptr>;
using atom = immer::atom<map>;      // thread-safe оболочка с compare-exchange

// ---------- статистика ----------
struct counters {
	std::atomic<uint64_t> get_total{ 0 }, set_total{ 0 };
	std::atomic<uint64_t> get_window{ 0 }, set_window{ 0 };

	void add_get() { ++get_total; ++get_window; }
	void add_set() { ++set_total; ++set_window; }

	void dump_and_reset();
};

class config_store {
public:
	explicit config_store(std::string file) : file_(std::move(file)) {
		root_.update([&](map m) {        // загрузка в атом
			load_into(m);
			return m;                   // первый снимок
		});
	}

	/* ---------- GET: 0-локов, 0-копий ---------- */
	std::optional<std::pair<const std::string&, entry_ptr>> get(const std::string& key);

	/* ---------- SET: path-copy без цикла ---------- */
	void set(const std::string& key, std::string value);

	bool flush_if_dirty();

	inline counters& get_stats() { return stats; }

private:
	void load_into(map& m);

	std::string file_;
	atom root_;                         // lock-free хранилище
	std::atomic<bool> dirty_{ false };
	counters stats;            // статистика запросов
};
