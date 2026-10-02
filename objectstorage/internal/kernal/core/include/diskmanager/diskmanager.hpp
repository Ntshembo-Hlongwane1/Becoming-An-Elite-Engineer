#pragma once 
#include <string>
#include "internal/kernal/core/include/types/core.hpp"
#include "internal/kernal/core/include/types/put-response.hpp"
#include <vector>
#include "internal/kernal/core/include/utility/result.hpp"
#include <system_error>
#include "internal/kernal/core/include/types/storage-error.hpp"
#include <dirent.h>


class DiskManager {

    public:
        DiskManager(const std::string rootDir);
        ~DiskManager();
        [[nodiscard]] Result<bool, std::error_code> CreateRootDir();
        [[nodiscard]] Result<bool, std::error_code> IsRootDirCreated();
        [[nodiscard]] Result<PutObjectResponse, std::error_code> PutObject(const std::vector<char> data, UID user);
        [[nodiscard]] Result<bool, std::error_code> RemoveObject();

    private:
        std::string rootPath_;
        int fd_;
        int folderCount_;

        void closeDir_(DIR* dir);
        void closeFile_(const std::string path);

        void CreateUserObject(const std::string path);
        void DeleteUserObject(const std::string path);
        
};