#include "diskmanager.hpp"
#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
    #include <io.h>
    #define PORTABLE_FSEEK _fseeki64
    #define PORTABLE_FTELL _ftelli64
#else
    #include <unistd.h>
    #define PORTABLE_FSEEK fseeko
    #define PORTABLE_FTELL ftello
#endif

DiskManager::DiskManager(const std::string& path) : m_Path(path){
    m_File = std::fopen(path.c_str(), "r+b");

    if (m_File == nullptr){
        m_File = std::fopen(path.c_str(), "w+b");
        if (m_File == nullptr){
            throw std::runtime_error("Failed to open or create: " + path);
        }
    }

    std::setvbuf(m_File, nullptr, _IOFBF, 0);
}

DiskManager::~DiskManager() {
    if (m_File != nullptr){
        std::fflush(m_File);
        std::fclose(m_File);
        m_File = nullptr;
    }
}

std::int64_t DiskManager::OffsetOf(page_id_t pageId) {
    return static_cast<std::int64_t>(pageId) * static_cast<std::int64_t>(PAGE_SIZE);
}

int DiskManager::Seek(std::int64_t offset) {
    return PORTABLE_FSEEK(m_File, offset, SEEK_SET);
}

void DiskManager::ReadPage(page_id_t pageId, Page& out) {

    if (pageId == INVALID_PAGE_ID){
        throw std::runtime_error("DiskManager::ReadPage: INVALID_PAGE_ID");
    }

    
    if (Seek(OffsetOf(pageId)) != 0){
        throw std::runtime_error("DiskManager::ReadPage: Seek failed on page " + std::to_string(pageId));
    }

    const std::size_t bytesRead = std::fread(out.data, 1, PAGE_SIZE, m_File);
    ++m_Reads;

    if (bytesRead != PAGE_SIZE){
        return;
    }

    if (std::ferror(m_File)){
        std::clearerr(m_File);
        throw std::runtime_error("DiskManager::ReadPage: Read failed on page " + std::to_string(pageId));
    }

    // Past EOF: an allocated but never written page. Zero-fill never expose stale bytes
    std::clearerr(m_File);
    std::memset(out.data + bytesRead, 0, PAGE_SIZE - bytesRead);
}

void DiskManager::WritePage(page_id_t pageId, const Page& in) {
    if (pageId == INVALID_PAGE_ID){
        throw std::runtime_error("DiskManager::WritePage: INVALID_PAGE_ID");
    }

    if (Seek(OffsetOf(pageId)) != 0){
        throw std::runtime_error("DiskManager::WritePage: Seek failed on page " + std::to_string(pageId));
    }

    const std::size_t bytesWritten = std::fwrite(in.data, 1, PAGE_SIZE, m_File);
    ++m_Writes;

    if (bytesWritten != PAGE_SIZE){
        throw std::runtime_error("DiskManager::WritePage: Write failed on page " + std::to_string(pageId));
    }

    std::fflush(m_File);
}

void DiskManager::Sync() {
    std::fflush(m_File);

    #if defined(_WIN32)
        if (_commit(_fileno(m_File)) != 0){
            throw std::runtime_error("DiskManager::Sync: _commit failed");
        }
    #else
        if (::fsync(::fileno(m_File)) != 0){
            throw std::runtime_error("DiskManager::Sync: fsync failed");
        }
    #endif
}

std::size_t DiskManager::NumPages() const {
    if (PORTABLE_FSEEK(m_File, 0, SEEK_END) != 0){
        throw std::runtime_error("DiskManager::NumPages: Seek failed");
    }

    const std::int64_t fileSize = PORTABLE_FTELL(m_File);

    if (fileSize < 0){
        throw std::runtime_error("DiskManager::NumPages: ftell failed");
    }

    return static_cast<std::size_t>(fileSize / PAGE_SIZE);
}

page_id_t DiskManager::AllocatePage() {
    if (m_Header.freeListHead != INVALID_PAGE_ID){
        const page_id_t id = m_Header.freeListHead;

        Page page;
        ReadPage(id, page);
        page_id_t next;
        std::memcpy(&next, page.data, sizeof(next));

        m_Header.freeListHead = next;
        --m_Header.numFreePages;
        m_HeaderDirty = true;

        // Hand back a clean page. Without this caller inherits previous occupant's
        std::memset(page.data, 0, PAGE_SIZE);
        WritePage(id, page);
        return id;
    }
}