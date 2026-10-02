#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "placementmanager.hpp"
#include "internal/kernal/core/include/types/core.hpp"
#include <stdexcept>
#include <exception>

PlacementManager::PlacementManager(){

};

PlacementManager::~PlacementManager(){

};

[[nodiscard]] int PlacementManager::GetFolderPlacement(const boost::uuids::uuid& uploadSessionKey, UID uid) noexcept {

    std::uint64_t timestamp = GetTimeStampMs_(uploadSessionKey);
    std::string timestampUTC = MsTimeStampToUTC_(timestamp);

    std::optional<WeekdayInfo> dayOfWeek = WeekdayFromUTC_(timestampUTC);

    if (!dayOfWeek){
        return -1;
    };

    const auto [isoNum, day] = *dayOfWeek;

    return isoNum;
};

std::string PlacementManager::GetObjectPath(ObjectId oid) noexcept {
    return "";
};

std::uint64_t PlacementManager::GetTimeStampMs_(const boost::uuids::uuid& uuid) noexcept{
    std::uint64_t timestamp = 0;

    for (size_t i = 0; i < 6; ++i){
        timestamp = (timestamp << 8) | static_cast<std::uint64_t>(uuid.data[i]);
    };
    

    return timestamp;

};

std::string PlacementManager::MsTimeStampToUTC_(std::uint64_t epoch_ms) {
    using namespace std::chrono;

    // Build a time_point from a duration of milliseconds
    const sys_time<milliseconds> tp{milliseconds{epoch_ms}};

    // Split into whole days + time-of-day so we can format both
    const auto day_point = floor<days>(tp);
    const year_month_day ymd{day_point};
    const hh_mm_ss tod{tp - day_point};

    std::ostringstream os;
    os << std::setfill('0')
       << std::setw(4) << static_cast<int>(ymd.year())  << '-'
       << std::setw(2) << static_cast<unsigned>(ymd.month()) << '-'
       << std::setw(2) << static_cast<unsigned>(ymd.day())   << ' '
       << std::setw(2) << tod.hours().count()   << ':'
       << std::setw(2) << tod.minutes().count() << ':'
       << std::setw(2) << tod.seconds().count() << '.'
       << std::setw(3) << tod.subseconds().count()
       << " UTC";
    return os.str();
}

std::optional<WeekdayInfo> PlacementManager::WeekdayFromUTC_(const std::string& utc) {
     using namespace std::chrono;

    std::istringstream is{utc};

    int      y = 0;
    unsigned mo = 0, d = 0;
    int      h = 0, mi = 0, s = 0, ms = 0;
    char     dash1 = 0, dash2 = 0, colon1 = 0, colon2 = 0, dot = 0;
    std::string tz;

    is >> y >> dash1 >> mo >> dash2 >> d
       >> h >> colon1 >> mi >> colon2 >> s >> dot >> ms
       >> tz;

    if (!is || dash1 != '-' || dash2 != '-' ||
        colon1 != ':' || colon2 != ':' || dot != '.') {
        return std::nullopt;
    }

    const year_month_day ymd{year{y}, month{mo}, day{d}};
    if (!ymd.ok()) return std::nullopt;

    const weekday wd{sys_days{ymd}};
    if (!wd.ok()) return std::nullopt;

    const auto w = static_cast<Weekday>(wd.c_encoding());
    return WeekdayInfo{ iso_number(w), w };
}