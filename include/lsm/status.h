#pragma once

#include <string>
#include <utility>

namespace lsm {

enum class StatusCode {
    OK = 0,
    NOT_FOUND,
    IO_ERROR,
    CORRUPTION,
    INVALID_ARGUMENT
};

class Status {
public:
    Status() : code_(StatusCode::OK), message_("") {}
    Status(StatusCode code, std::string message = "")
        : code_(code), message_(std::move(message)) {}

    static Status OK() { return Status(StatusCode::OK); }
    static Status NotFound(std::string msg = "Key not found") {
        return Status(StatusCode::NOT_FOUND, std::move(msg));
    }
    static Status IOError(std::string msg) {
        return Status(StatusCode::IO_ERROR, std::move(msg));
    }
    static Status Corruption(std::string msg) {
        return Status(StatusCode::CORRUPTION, std::move(msg));
    }
    static Status InvalidArgument(std::string msg) {
        return Status(StatusCode::INVALID_ARGUMENT, std::move(msg));
    }

    bool ok() const { return code_ == StatusCode::OK; }
    bool isNotFound() const { return code_ == StatusCode::NOT_FOUND; }
    bool isIOError() const { return code_ == StatusCode::IO_ERROR; }
    bool isCorruption() const { return code_ == StatusCode::CORRUPTION; }
    bool isInvalidArgument() const { return code_ == StatusCode::INVALID_ARGUMENT; }

    StatusCode code() const { return code_; }
    const std::string& message() const { return message_; }

    std::string toString() const {
        if (code_ == StatusCode::OK) return "OK";
        std::string prefix;
        switch (code_) {
            case StatusCode::NOT_FOUND: prefix = "NotFound: "; break;
            case StatusCode::IO_ERROR: prefix = "IOError: "; break;
            case StatusCode::CORRUPTION: prefix = "Corruption: "; break;
            case StatusCode::INVALID_ARGUMENT: prefix = "InvalidArgument: "; break;
            default: prefix = "UnknownError: "; break;
        }
        return prefix + message_;
    }

private:
    StatusCode code_;
    std::string message_;
};

struct Result {
    Status status;
    std::string value;

    Result(Status s, std::string v = "") : status(std::move(s)), value(std::move(v)) {}

    static Result OK(std::string value) {
        return Result(Status::OK(), std::move(value));
    }
    static Result NotFound(std::string msg = "Key not found") {
        return Result(Status::NotFound(std::move(msg)));
    }
    static Result Error(Status s) {
        return Result(std::move(s));
    }

    bool ok() const { return status.ok(); }
};

} // namespace lsm
