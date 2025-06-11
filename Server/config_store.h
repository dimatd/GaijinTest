#pragma once

#include <immer/map.hpp>      // persistent RB-tree
#include <immer/atom.hpp>     // lock-free атом с CAS
#include <atomic>
#include <filesystem>
#include <string>
#include <optional>

struct Entry {
	std::string value;
	std::atomic<uint64_t> reads{ 0 }, writes{ 0 };
};

using EntryPtr = std::shared_ptr<Entry>;
using Map = immer::map<std::string, EntryPtr>;
using Atom = immer::atom<Map>;      // thread-safe оболочка с compare-exchange

// ---------- статистика ----------
struct Counters {
	std::atomic<uint64_t> get_total{ 0 }, set_total{ 0 };
	std::atomic<uint64_t> get_window{ 0 }, set_window{ 0 };

	void add_get() { ++get_total; ++get_window; }
	void add_set() { ++set_total; ++set_window; }

	void dump_and_reset();
};

class ConfigStore {
public:
	explicit ConfigStore(std::string file) : file_(std::move(file)) {
		root_.update([&](Map m) {        // загрузка в атом
			load_into(m);
			return m;                   // первый снимок
		});
	}

	/* ---------- GET: 0-локов, 0-копий ---------- */
	std::optional<std::pair<const std::string&, EntryPtr>> get(const std::string& key);

	/* ---------- SET: path-copy без цикла ---------- */
	void set(const std::string& key, std::string value);

	bool flush_if_dirty();

private:
	void load_into(Map& m);

	std::string file_;
	Atom root_;                         // lock-free хранилище
	std::atomic<bool> dirty_{ false };
	Counters stats;            // статистика запросов
};
