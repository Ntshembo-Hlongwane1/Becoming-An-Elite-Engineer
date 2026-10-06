#pragma once 
#include <iostream>
#include <string>
#include <iomanip>
#include <sstream>
#include <memory>
#include <array>
#include <span>
#include <string_view>
#include <stdexcept>
#include <algorithm>
#include <openssl/evp.h>
#include <boost/uuid/uuid.hpp>
#include "internal/kernal/core/include/types/core.hpp"

namespace utility::crypto {

    std::string inline sha256(const std::string& input) {
        // 1. Initialize OpenSSL digest context using a smart pointer for automatic cleanup
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if (!context) {
            throw std::runtime_error("Failed to create OpenSSL EVP digest context.");
        }

        // 2. Initialize the digest context to use the SHA-256 algorithm
        if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
            throw std::runtime_error("Failed to initialize SHA-256 digest.");
        }

        // 3. Pass the data to be hashed into the context
        if (EVP_DigestUpdate(context.get(), input.c_str(), input.length()) != 1) {
            throw std::runtime_error("Failed to update SHA-256 digest with input data.");
        }

        // 4. Allocate buffer to store the raw binary hash (SHA-256 produces 32 bytes)
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int length = 0;

        // 5. Finalize the hash and extract the binary results
        if (EVP_DigestFinal_ex(context.get(), hash, &length) != 1) {
            throw std::runtime_error("Failed to finalize SHA-256 digest.");
        }

        // 6. Convert the binary hash array into a standard 64-character hex string
        std::stringstream string_builder;
        for (unsigned int i = 0; i < length; ++i) {
            string_builder << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }

        return string_builder.str();
    }

    // Constant-time comparison to avoid timing attacks
    inline bool secure_compare(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        unsigned char diff = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            diff |= static_cast<unsigned char>(a[i] ^ b[i]);
        }
        return diff == 0;
    }

    // Validate that `input` matches a known expected hash
    inline bool validate_sha256(const std::string& input, const std::string& expected_hash) {
        return secure_compare(sha256(input), expected_hash);
    }

    using Digest256 = std::array<unsigned char, 32>;

    // HMAC-SHA-256 (FIPS 198-1 / FIPS 180-4) via the OpenSSL 3 provider API
    inline Digest256 hmac_sha256(std::span<const unsigned char> key, std::span<const unsigned char> msg) {
        Digest256 out{};
        size_t length = 0;

        if (EVP_Q_mac(nullptr, "HMAC", nullptr, "SHA256", nullptr,
                      key.data(), key.size(), msg.data(), msg.size(),
                      out.data(), out.size(), &length) == nullptr || length != out.size()) {
            throw std::runtime_error("Failed to compute HMAC-SHA256.");
        }

        return out;
    }

    // Deterministic ObjectId from an upload session key:
    // hex( HMAC-SHA-256(serverKey, "objstore/objectid/v1" || 16 raw uuid bytes)[0..16] )
    // Same session + same serverKey => same ObjectId. 128 bits, 32 lowercase hex chars.
    inline ObjectId make_object_id(std::span<const unsigned char> serverKey, const boost::uuids::uuid& sessionKey) {
        static constexpr std::string_view kTag = "objstore/objectid/v1";

        // Tag is fixed-length and the uuid is always 16 bytes, so the encoding is unambiguous
        std::array<unsigned char, kTag.size() + 16> msg{};
        std::copy(kTag.begin(), kTag.end(), msg.begin());
        std::copy(sessionKey.begin(), sessionKey.end(), msg.begin() + kTag.size());

        const Digest256 digest = hmac_sha256(serverKey, msg);

        // Truncate to 128 bits (permitted by NIST SP 800-107) and hex encode
        static constexpr char hex[] = "0123456789abcdef";
        ObjectId id;
        id.reserve(32);
        for (size_t i = 0; i < 16; ++i) {
            id.push_back(hex[digest[i] >> 4]);
            id.push_back(hex[digest[i] & 0x0f]);
        }

        return id;
    }

}