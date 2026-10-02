#include <iostream>
#include "diskmanager.hpp"
#include <string>
#include "internal/kernal/core/include/utility/result.hpp"
#include "internal/kernal/core/include/types/put-response.hpp"
#include "internal/kernal/core/include/types/storage-error.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <dirent.h>
#include <cerrno>


DiskManager::DiskManager(const std::string rootDir) : rootPath_(rootDir), fd_(-1), folderCount_(0){};

DiskManager::~DiskManager(){

};

[[nodiscard]] Result<bool, std::error_code> DiskManager::CreateRootDir(){
    if (::mkdir(rootPath_.c_str(), 0755) == 0){
        return true;
    };

    if (errno == EEXIST){
        return std::error_code{StorageErrc::AlreadyExists};
    }

    return true;
};

[[nodiscard]] Result<bool, std::error_code> DiskManager::IsRootDirCreated(){
    DIR* dir = ::opendir(rootPath_.c_str());

    if (dir == nullptr){
        return false;
    }

    closeDir_(dir);
    return true;
}

void DiskManager::closeDir_(DIR* dir){
    int res = ::closedir(dir);

    if (res == -1){
        throw std::runtime_error("Failed to close DIR");
    }
}


[[nodiscard]] Result<PutObjectResponse, std::error_code> DiskManager::PutObject(const std::vector<char> data, UID user){
    std::cout << "PUT OBJECT" << std::endl;
    std::cout << "USER: " << user << std::endl;
    (void)data;

    // TODO: not implemented yet
    return std::make_error_code(std::errc::function_not_supported);
}

[[nodiscard]] Result<bool, std::error_code> DiskManager::RemoveObject(){
    std::cout << "REMOVE OBJECT" << std::endl;

    // TODO: not implemented yet
    return std::make_error_code(std::errc::function_not_supported);
}
