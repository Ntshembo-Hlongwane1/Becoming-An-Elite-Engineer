#include "store.hpp"
#include "internal/kernal/core/include/types/upload-session.hpp"
#include <chrono>
#include <iostream>


[[nodiscard]] bool Store::IsSessionsEmpty() const noexcept {
    return currentSessions_.empty();
};


[[nodiscard]] bool Store::TryInsertNewSession(UploadSession& session) noexcept {
    if (currentSessions_.contains(session.sessionKey)){
        return false;
    }

    if (std::chrono::system_clock::now() > session.expiresAt){
        return false;
    }

    currentSessions_.try_emplace(session.sessionKey, session);
    return true;
}

[[nodiscard]] bool IsSessionKeyValid(UploadSessionKey& key) noexcept {
    std::cout << key << std::endl;
    return true;
}
