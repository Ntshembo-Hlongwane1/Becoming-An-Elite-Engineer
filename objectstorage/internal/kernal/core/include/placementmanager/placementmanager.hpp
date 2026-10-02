#pragma once

#include <cstdint>
#include <iostream>
#include <utility>
#include <string>
#include "internal/kernal/core/include/types/core.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <optional>

enum class Weekday : std::uint8_t {
    Sunday    = 0,
    Monday    = 1,
    Tuesday   = 2,
    Wednesday = 3,
    Thursday  = 4,
    Friday    = 5,
    Saturday  = 6,
};

using WeekdayInfo = std::pair<unsigned, Weekday>;

class PlacementManager {

    public:
        PlacementManager();
        ~PlacementManager();

        [[nodiscard]] int GetFolderPlacement(const boost::uuids::uuid& uploadSessionKey, UID uid) noexcept;
        [[nodiscard]] std::string GetObjectPath(ObjectId oid) noexcept;

    private:
        [[nodiscard]] std::uint64_t GetTimeStampMs_(const boost::uuids::uuid& uuid) noexcept;
        [[nodiscard]] std::string MsTimeStampToUTC_(std::uint64_t epoch_ms);
        [[nodiscard]] std::optional<WeekdayInfo> WeekdayFromUTC_(const std::string& utc);

        static constexpr unsigned iso_number(Weekday w) noexcept {
            const auto c = static_cast<unsigned>(w);
            return (c == 0) ? 7u : c;
        }

};
