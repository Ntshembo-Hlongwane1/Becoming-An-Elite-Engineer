#include <iostream>
#include "crow.h"
#include <unordered_map>
#include <string>
#include "internal/kernal/core/include/types/upload-session-mint.hpp"
#include <utility>
#include "internal/kernal/core/include/types/core.hpp"
#include "internal/kernal/core/include/utility/crypto.hpp"
#include "internal/kernal/core/include/diskmanager/diskmanager.hpp"
#include <stdexcept>
#include <cerrno>
#include "internal/kernal/core/include/types/storage-error.hpp"
#include <boost/uuid/time_generator_v7.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <array>
#include <cstdlib>
#include <string_view>

// Decode the 64-char hex OBJSTORE_SERVER_KEY env var into 32 raw bytes; fail fast if missing/malformed
// Generate once with: openssl rand -hex 32
std::array<unsigned char, 32> LoadServerKey() {
    const char* env = std::getenv("OBJSTORE_SERVER_KEY");
    if (!env || std::string_view(env).size() != 64) {
        throw std::runtime_error("OBJSTORE_SERVER_KEY must be 64 hex chars (openssl rand -hex 32)");
    }

    // Strict nibble decode: rejects anything that isn't [0-9a-fA-F]
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::array<unsigned char, 32> key{};
    for (size_t i = 0; i < key.size(); ++i) {
        const int hi = nibble(env[2 * i]);
        const int lo = nibble(env[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            throw std::runtime_error("OBJSTORE_SERVER_KEY contains non-hex characters");
        }
        key[i] = static_cast<unsigned char>((hi << 4) | lo);
    }

    return key;
}

struct User {
    std::string email;
    std::string username;
    std::string uuid;
};

std::vector<User> users = {
    {
        "james.mokoena@example.com",
        "james_mokoena",
        "550e8400-e29b-41d4-a716-446655440000"
    },
    {
        "sarah.naidoo@example.com",
        "sarah_naidoo",
        "7f3c9a21-6b84-4e17-8d52-91c4a7b3e6f0"
    },
    {
        "thabo.baloyi@example.com",
        "thabo_baloyi",
        "c4a1f8e2-3d67-4b95-9c21-7e5a0f6d8b34"
    },
    {
        "lisa.vanwyk@example.com",
        "lisa_vanwyk",
        "1b6e3c92-8f45-4a71-bd63-52c9e7f014a8"
    },
    {
        "daniel.mthembu@example.com",
        "daniel_mthembu",
        "9a72d5f4-1c38-4e06-8b91-63f7a2c5d849"
    },
    {
        "amina.patel@example.com",
        "amina_patel",
        "e83f2a61-5479-4c08-ae35-71d9b6f402ca"
    }
};

int main(){
    std::cout << "Object Storage" << std::endl;

    //TEMP WILL BE REPLACED WITH DB;
    std::unordered_map<UploadSessionKey, UID> uploadSessions;
    std::unordered_map<UploadSessionKey, ObjectId> sessionObjects;
    const auto serverKey = LoadServerKey();
    
    crow::SimpleApp app;

    DiskManager dm{"data"};

    auto rootCreateResponse = dm.CreateRootDir();

    if (!rootCreateResponse){
        if (rootCreateResponse.error() == StorageErrc::AlreadyExists){
            std::cout << "\n Root Dir already exists. Not re-creating." << std::endl;
        };

    }else {
        std::cout << "Root DIR created." << std::endl;
    }

    CROW_ROUTE(app, "/status")([](){
        return "ALive";
    });

    CROW_ROUTE(app, "/upload").methods("POST"_method)([&uploadSessions, &sessionObjects, &serverKey](const crow::request& request){

        try{
            auto body = crow::json::load(request.body);

            if (!body){
                return crow::response(400, "Bad requst");
            };

            std::cout << "UID: " << body["uid"] << std::endl;

            crow::json::wvalue res;
            UID uid  = body["uid"].s();
            // Generator holds per-instance state (last ms + counter), so one per Crow worker thread
            thread_local boost::uuids::time_generator_v7 uuidGen;
            boost::uuids::uuid session = uuidGen();
            UploadSessionKey key = boost::uuids::to_string(session);

            // Deterministic ObjectId derived from the session, stored at mint time
            ObjectId oid = utility::crypto::make_object_id(serverKey, session);

            res["upload_session"] = key;

            uploadSessions.try_emplace(key, uid);
            sessionObjects.try_emplace(key, oid);

            return crow::response(200, res);
        }catch(const std::exception& e){
            std::cout << e.what() << std::endl;
            return crow::response(500, e.what());
        }

    });

    CROW_ROUTE(app, "/upload/<string>/chunk")
      .methods("POST"_method)
      ([&dm, &uploadSessions, &sessionObjects](const crow::request& request, UploadSessionKey uploadSessionKey){


        std::vector<char> data(request.body.begin(), request.body.end());

        auto it = uploadSessions.find(uploadSessionKey);

        if (it == uploadSessions.end()){
            return crow::response(400);
        };

        auto oidIt = sessionObjects.find(uploadSessionKey);

        if (oidIt != sessionObjects.end()){
            std::cout << "Session: " << uploadSessionKey << " -> OID: " << oidIt->second << std::endl;
        };

        auto res = dm.PutObject(data, it->second);

        return crow::response(200);
    });

    app.port(3001).multithreaded().run();
    return 0;
}