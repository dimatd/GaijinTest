#pragma once
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include <type_traits>
#include <limits>

class memory_writer {
public:
    memory_writer() = default;
    explicit memory_writer(std::vector<uint8_t>&& buffer) : buffer_(std::move(buffer)) {}

    void write(const std::string& str) {
        if (str.size() > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("string too long to serialize");
        }
        uint32_t len = static_cast<uint32_t>(str.size());
        write(len);
        buffer_.insert(buffer_.end(), str.begin(), str.end());
    }

    template<typename T>
    void write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
        buffer_.insert(buffer_.end(), ptr, ptr + sizeof(T));
    }

    inline const std::vector<uint8_t>& get_buffer() const {
        return buffer_;
    }

private:
    std::vector<uint8_t> buffer_;
};

class memory_reader {
public:
    explicit memory_reader(const std::vector<uint8_t>& buffer) : buffer_(buffer), offset_(0) {}

    void read(std::string& value) {
        uint32_t len;
        read(len);
        if (offset_ + len > buffer_.size()) {
            throw std::runtime_error("string read out of bounds");
        }
        value = std::string(reinterpret_cast<const char*>(buffer_.data() + offset_), len);
        offset_ += len;
    }

    template<typename T>
    void read(T& value) {
        if (offset_ + sizeof(T) > buffer_.size()) {
            throw std::runtime_error("read out of bounds");
        }
        std::memcpy(&value, buffer_.data() + offset_, sizeof(T));
        offset_ += sizeof(T);
    }

    template<typename T>
    T read_val() {
        T value;
        read(value);
        return value;
    }

    bool is_end() const {
        return offset_ >= buffer_.size();
    }

    size_t size() const {
        return buffer_.size() - offset_;
    }

private:
    const std::vector<uint8_t>& buffer_;
    size_t offset_;
};
