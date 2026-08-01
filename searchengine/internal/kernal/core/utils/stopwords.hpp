#pragma once
#include <string_view>
#include <unordered_set>
#include <string>
#include <cctype>

namespace StopWords {

    struct ci_hash {
        using is_transparent = void;  

        size_t operator()(std::string_view sv) const noexcept {
            size_t h = 0;
            for (unsigned char c : sv) {
                c = static_cast<unsigned char>(std::tolower(c));
                h = h * 31 + c;
            }
            return h;
        }

        size_t operator()(const std::string& s) const noexcept {
            return (*this)(std::string_view(s));
        }
    };

    // Custom equality for case‑insensitive comparison
    struct ci_equal {
        using is_transparent = void;

        bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
            if (lhs.size() != rhs.size()) return false;
            for (size_t i = 0; i < lhs.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
                    std::tolower(static_cast<unsigned char>(rhs[i])))
                    return false;
            }
            return true;
        }

        bool operator()(const std::string& lhs, const std::string& rhs) const noexcept {
            return (*this)(std::string_view(lhs), std::string_view(rhs));
        }
    };

    bool isStopWord(std::string_view word) noexcept;

}