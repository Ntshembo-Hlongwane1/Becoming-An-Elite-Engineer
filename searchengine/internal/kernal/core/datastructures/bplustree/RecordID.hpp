#pragma once
#include <cstdint>

struct RecordID{
    uint64_t documentID;

    bool operator==(const RecordID& other) const {
        return documentID == other.documentID;
    }
};
