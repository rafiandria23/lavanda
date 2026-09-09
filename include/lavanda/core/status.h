#ifndef LAVANDA_CORE_STATUS_H_
#define LAVANDA_CORE_STATUS_H_

#include <optional>
#include <string>
#include <utility>

namespace lavanda {

enum class ErrorCode {
  kOk = 0,
  kInvalidArgument,
  kDeviceUnavailable,
  kAlreadyOpen,
  kNotOpen,
  kPlatformError,
  kUnsupportedFormat,
  kQueueFull,
};

class Status {
 public:
  Status() noexcept : code_(ErrorCode::kOk) {}
  Status(ErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  static Status Ok() noexcept { return Status(); }

  bool ok() const noexcept { return code_ == ErrorCode::kOk; }
  ErrorCode code() const noexcept { return code_; }
  const std::string& message() const noexcept { return message_; }

 private:
  ErrorCode code_;
  std::string message_;
};

template <typename T>
class StatusOr {
 public:
  StatusOr(T value) : status_(Status::Ok()), value_(std::move(value)) {}
  StatusOr(Status status) : status_(std::move(status)) {}

  bool ok() const noexcept { return status_.ok(); }
  const Status& status() const noexcept { return status_; }

  T& value() noexcept { return *value_; }
  const T& value() const noexcept { return *value_; }

 private:
  Status status_;
  std::optional<T> value_;
};

}  // namespace lavanda

#endif
