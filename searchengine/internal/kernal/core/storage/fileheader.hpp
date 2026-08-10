#pragma once
#include <cstdint>
#include <cstdio>
#include "page.hpp"
#include <cstring>

struct FileHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t pageSize;
    std::uint32_t pageCount;
    page_id_t freeListHead;
    page_id_t rootPageId;
    std::uint32_t numFreePages;
    std::uint32_t reserved;
};

static void EncodeHeader(const FileHeader& header, Page& page){
    std::memset(page.data, 0, PAGE_SIZE);
    std::size_t offset = 0;
    auto put = [&](auto v) {
        std::memcpy(page.data + offset, &v, sizeof(v));
        offset += sizeof(v);
    };

    put(header.magic);
    put(header.version);
    put(header.pageSize);
    put(header.pageCount);
    put(header.freeListHead);
    put(header.rootPageId);
    put(header.numFreePages);
    put(header.reserved);
};

static FileHeader DecodeHeader(const Page& page) {
    FileHeader header{};
    std::size_t off = 0;
    auto get = [&](auto& v) { 
        std::memcpy(&v, page.data + off, sizeof(v));
        off += sizeof(v); 
    };

    get(header.magic);  
    get(header.version);  
    get(header.pageSize);     
    get(header.pageCount);
    get(header.freeListHead);  
    get(header.rootPageId);  
    get(header.numFreePages);  
    get(header.reserved);
    
    return header;
}

static_assert(sizeof(FileHeader) == 32, "FileHeader layout must be stable across builds");
static_assert(sizeof(FileHeader) <= PAGE_SIZE, "header must fit in page 0");

inline constexpr std::uint32_t SEDB_MAGIC   = 0x42444553u;
inline constexpr std::uint32_t SEDB_VERSION = 1u;