#include "lexer.hpp"
#include "internal/kernal/core/headerfiles/error.hpp"
#include <iostream>
#include <string_view>
#include "ilp.hpp"
#include <thread>



std::string Lexer::Name() {
    return "Lexer";
};

Error Lexer::Init(){
    std::cout << "["<< Name() << "] Initializing.." << std::endl;
    return Error("");
};

Error Lexer::Start(){
    std::cout << "\n [" << Name() << "] Starting..." << std::endl;
    try{
        worker1 = std::thread(&Lexer::worker, this, "1"); 
        worker2 = std::thread(&Lexer::worker, this, "2"); 
    }catch(const std::system_error& e){
        return Error("Thread creation failed" + std::string(e.what()));
    }    
    return Error("");
};

Error Lexer::Stop(){
    std::cout << "\n [" << Name() <<"] Stopping..." << std::endl;

   
    return Error("");
};

void Lexer::worker(std::string thread){
    std::cout << "WORKER STARTING: " << thread <<  std::endl;

    while(running.load(std::memory_order_acquire)){
        ILP line;

       { 
            std::unique_lock<std::mutex> lock(lineQueueMutex);

            queueCV.wait(lock, [this] { return !lineQueue.empty(); });

            if (!running.load(std::memory_order_acquire) && lineQueue.empty()) {
                break;
            }

            if (!lineQueue.pop(line)){
                continue;
            }
        }

       if (line.filepath.empty() && line.line.empty()) {
            std::cout << "Thread " << thread << " exiting." << std::endl;
            break;
        }

        // Process the line
        std::cout << "Thread: " << thread << " " 
                  << line.filepath << " : " << line.line << std::endl;
    }

}

void Lexer::Run(){
    std::cout << "[" << Name() << "] Run starting..." << std::endl;

    int dataFilesReceived = 0;

    while (true){
        std::string file;

        if (dirQueue.pop(file)){
            if (file == ""){
                std::cout << "\n [" << Name() << "] POISON PILL. Exiting" << std::endl;
                break;
            }

            dataFilesReceived++;

            if (file.empty()){
                break;
            }

            FILE* fp = fopen(file.c_str(), "r");

            if (!fp){
                std::cout << "\n [" << Name() << "] Failed to open data file" << std::endl;
            }

            char line[456];

            while(fgets(line, sizeof(line), fp) != nullptr){
                size_t len = strlen(line);

                if (len > 0 && line[len - 1] == '\n'){
                    line[len - 1] = '\0';
                }

                // std::cout << "Pushing line" << std::endl;
                std::lock_guard<std::mutex> lock(lineQueueMutex);
                lineQueue.push({ file, std::string(line) }); 

                queueCV.notify_one();
            }

            if (ferror(fp)) {
                std::cerr << "[" << Name() << "] Read error on file: " << file << std::endl;
            }

            fclose(fp);
            
        }
    }
    //running.store(false, std::memory_order_release);

};


void Lexer::SetFile(const std::string& filename){
    if (file && file != stdin){
        fclose(file);
        file = nullptr;
    };

    filepath = filename;

    line_count = 0;
    line[0] = '\0';

};

