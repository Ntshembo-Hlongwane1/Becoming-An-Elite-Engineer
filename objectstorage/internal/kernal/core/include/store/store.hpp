#pragma once
#include "internal/kernal/core/include/types/core.hpp"
#include <unordered_map>
#include "internal/kernal/core/include/types/upload-session.hpp"

using UploadSessions = std::unordered_map<UploadSessionKey, UploadSession>;

class Store {

    public:

        [[nodiscard]] bool IsSessionsEmpty() const noexcept;
        [[nodiscard]] bool TryInsertNewSession(UploadSession& session) noexcept;
        [[nodiscard]] bool IsSessionKeyValid(UploadSessionKey& key) noexcept;
        
    
    private:
        UploadSessions currentSessions_{};

};