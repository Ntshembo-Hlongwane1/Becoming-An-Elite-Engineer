#pragma once
#include <system_error>

enum class StorageErrc {
    None = 0,
    NotFound,
    AccessDenied,
    NetworkFailure,
    InvalidData,
    DirCreatetionFailure,
    ObjectCreationFailure,
    AlreadyExists,
    NotADirectory,
    NotEmpty,
    IoError,
    Uknown,
};

const std::error_category& storage_category() noexcept;
std::error_code make_error_code(StorageErrc e);

namespace std {
    template<>
    struct is_error_code_enum<StorageErrc> : true_type {};
}
