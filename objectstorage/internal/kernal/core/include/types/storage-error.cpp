#include "storage-error.hpp"
#include <string>

namespace {

class StorageCategory final : public std::error_category {
public:
    const char* name() const noexcept override { return "storage"; }

    std::string message(int ev) const override {
        switch (static_cast<StorageErrc>(ev)) {
            case StorageErrc::None:                  return "success";
            case StorageErrc::NotFound:              return "not found";
            case StorageErrc::AccessDenied:          return "access denied";
            case StorageErrc::NetworkFailure:        return "network failure";
            case StorageErrc::InvalidData:           return "invalid data";
            case StorageErrc::DirCreatetionFailure:  return "directory creation failed";
            case StorageErrc::ObjectCreationFailure: return "object creation failed";
            case StorageErrc::AlreadyExists:         return "already exists";
            case StorageErrc::NotADirectory:         return "not a directory";
            case StorageErrc::NotEmpty:              return "not empty";
            case StorageErrc::IoError:               return "I/O error";
            case StorageErrc::Uknown:                return "unknown error";
        }
        return "unrecognized storage error";
    }
};

} // namespace

const std::error_category& storage_category() noexcept {
    static const StorageCategory instance;
    return instance;
}

std::error_code make_error_code(StorageErrc e) {
    return {static_cast<int>(e), storage_category()};
}
