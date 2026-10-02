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

    CROW_ROUTE(app, "/upload").methods("POST"_method)([&uploadSessions](const crow::request& request){

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
            UploadSessionKey key = boost::uuids::to_string(uuidGen());

            res["upload_session"] = key;

            uploadSessions.try_emplace(key, uid);

            return crow::response(200, res);
        }catch(const std::exception& e){
            std::cout << e.what() << std::endl;
            return crow::response(500, e.what());
        }

    });

    CROW_ROUTE(app, "/upload/<string>/chunk")
      .methods("POST"_method)
      ([&dm, &uploadSessions](const crow::request& request, UploadSessionKey uploadSessionKey){
         

        std::vector<char> data(request.body.begin(), request.body.end());

        auto it = uploadSessions.find(uploadSessionKey);

        if (it == uploadSessions.end()){
            return crow::response(400);
        };

        auto res = dm.PutObject(data, it->second);

        return crow::response(200);
    });

    app.port(3001).multithreaded().run();
    return 0;
}