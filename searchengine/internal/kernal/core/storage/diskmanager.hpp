#pragma once
#include <string>
#include <cstdint>
#include <cstdio>
#include "page.hpp"
#include "fileheader.hpp"

class DiskManager {
    public:
        explicit DiskManager(const std::string& path);
        ~DiskManager();

        DiskManager(const DiskManager&) = delete;
        DiskManager& operator=(const DiskManager&) = delete;

        void ReadPage(page_id_t pageId, Page& out);
        void WritePage(page_id_t pageId, const Page& in);
        void Sync();
        page_id_t AllocatePage();
        void DeallocatePage(page_id_t id);

        std::size_t NumPages() const;
        const std::string& Path() const {
           return  m_Path;
        };

        std::uint64_t ReadCount() const {
            return m_Reads;
        }

        std::uint64_t WriteCount() const {
            return m_Writes;
        }

        private:
            static std::int64_t OffsetOf(page_id_t pageId);
            int Seek(std::int64_t offset);

            std::string m_Path;
            std::FILE* m_File;
            std::uint64_t m_Reads = 0;
            std::uint64_t m_Writes = 0;
            FileHeader m_Header;
            bool m_HeaderDirty = false;
};