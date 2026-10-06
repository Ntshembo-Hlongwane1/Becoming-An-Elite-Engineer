#pragma once
// Provided. A tiny error type in the spirit of leveldb::Status: storage code reports
// corruption and I/O errors as values, not exceptions, because they are *expected* outcomes
// when reading bytes you don't fully trust (notes Part 7 §2).
#include <string>
#include <utility>

namespace lsm {

class Status {
public:
    enum class Code { kOk, kNotFound, kCorruption, kIOError, kInvalidArgument };

    Status() = default;
    static Status OK() { return {}; }
    static Status NotFound(std::string msg = {}) { return {Code::kNotFound, std::move(msg)}; }
    static Status Corruption(std::string msg) { return {Code::kCorruption, std::move(msg)}; }
    static Status IOError(std::string msg) { return {Code::kIOError, std::move(msg)}; }
    static Status InvalidArgument(std::string msg) { return {Code::kInvalidArgument, std::move(msg)}; }

    bool ok() const { return code_ == Code::kOk; }
    bool IsNotFound() const { return code_ == Code::kNotFound; }
    bool IsCorruption() const { return code_ == Code::kCorruption; }
    bool IsIOError() const { return code_ == Code::kIOError; }
    Code code() const { return code_; }
    const std::string& message() const { return msg_; }
    std::string ToString() const;

private:
    Status(Code c, std::string m) : code_(c), msg_(std::move(m)) {}
    Code code_ = Code::kOk;
    std::string msg_;
};

inline std::string Status::ToString() const {
    switch (code_) {
        case Code::kOk: return "OK";
        case Code::kNotFound: return "NotFound: " + msg_;
        case Code::kCorruption: return "Corruption: " + msg_;
        case Code::kIOError: return "IOError: " + msg_;
        case Code::kInvalidArgument: return "InvalidArgument: " + msg_;
    }
    return "Unknown";
}

}  // namespace lsm
