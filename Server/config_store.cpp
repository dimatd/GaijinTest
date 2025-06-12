#include "config_store.h"
#include <fstream>
#include <iostream>
#include <immer/map_transient.hpp> // для загрузки в временную версию дерева

std::optional<std::pair<const std::string&, entry_ptr>> config_store::get(const std::string& key) {
	auto snap = root_.load();               // захватываем «снимок» RB-дерева
	auto entry_ptr = snap->find(key);
	if(entry_ptr == nullptr) return std::nullopt;

	(*entry_ptr)->reads++;      // atomic++
	stats.add_get();
	return { {key, *entry_ptr} };
}

void config_store::set(const std::string& key, std::string value) {
	root_.update([&](map m) {
		auto entry_ptr_ptr = m.find(key);
		entry_ptr entry_ = entry_ptr_ptr != nullptr ? *entry_ptr_ptr : std::make_shared<entry>();
		entry_->value = std::move(value);
		entry_->writes++;
		return m.set(key, std::move(entry_));   // ← создаётся новое дерево, разделяя 99 % узлов
	});
	stats.add_set();
	dirty_.store(true, std::memory_order_relaxed);
}

bool config_store::flush_if_dirty()
{
	if(!dirty_.exchange(false)) return false;

	std::ofstream out(file_, std::ios::binary);
	if (!out) {
		throw std::ios_base::failure("Failed to open file for writing");
	}

	auto snap = root_.load();
	auto data = *snap;
	size_t size = data.size();
	out.write(reinterpret_cast<const char*>(&size), sizeof(size)); // Записываем количество пар

	for (const auto& [key, entry] : data) {
		size_t key_size = key.size();
		size_t value_size = entry->value.size();

		out.write(reinterpret_cast<const char*>(&key_size), sizeof(key_size));     // Размер ключа
		out.write(key.data(), key_size);                                             // Ключ
		out.write(reinterpret_cast<const char*>(&value_size), sizeof(value_size)); // Размер значения
		out.write(entry->value.data(), value_size);                                         // Значение
	}

	out.flush(); // Сбрасываем буфер в файл
	if(!out) {
		throw std::ios_base::failure("Failed to write data to file");
	}

	return true; // Успешно сбросили данные в файл
}

void config_store::load_into(map& m)
{
	std::ifstream in(file_, std::ios::binary | std::ios::in);
	if (!in) {
		return;
	}

	auto t = m.transient(); // Получаем временную версию дерева для загрузки
	//std::unordered_map<std::string, std::string> data;
	size_t size;
	in.read(reinterpret_cast<char*>(&size), sizeof(size)); // Читаем количество пар

	for (size_t i = 0; i < size; ++i) {
		size_t key_size, value_size;
		in.read(reinterpret_cast<char*>(&key_size), sizeof(key_size)); // Размер ключа

		std::string key(key_size, '\0');
		in.read(key.data(), key_size); // Ключ

		in.read(reinterpret_cast<char*>(&value_size), sizeof(value_size)); // Размер значения

		std::string value(value_size, '\0');
		in.read(value.data(), value_size); // Значение

		entry_ptr entry_ = std::make_shared<entry>();
		entry_->value = std::move(value);

		t.set(std::move(key), std::move(entry_));
	}

	m = t.persistent();
}

void counters::dump_and_reset()
{
	std::cout << "[Stats] total: GET=" << get_total
		<< " SET=" << set_total
		<< " | last 5s: GET=" << get_window
		<< " SET=" << set_window << '\n';
	get_window = set_window = 0;
}