#pragma once 
#include <iostream>
#include <string>
#include <iomanip>
#include <sstream>
#include <memory>
#include <openssl/evp.h>

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
    bool secure_compare(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        unsigned char diff = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            diff |= static_cast<unsigned char>(a[i] ^ b[i]);
        }
        return diff == 0;
    }

    // Validate that `input` matches a known expected hash
    bool validate_sha256(const std::string& input, const std::string& expected_hash) {
        return secure_compare(sha256(input), expected_hash);
    }
}