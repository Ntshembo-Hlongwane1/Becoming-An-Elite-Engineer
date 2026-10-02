#pragma once 
#include <utility>
#include <optional>
#include <iostream>
#include <string>

template<typename T, typename E = std::string>
class Result {

    public:
        Result(T v) : value_(std::move(v)){}
        Result(E e) : error_(std::move(e)){}

        bool isOk() const {
            return value_.has_value();
        }

        explicit operator bool() const { 
            return isOk();
        };

        T& value() {
            return *value_;
        }

        const T& value() const {
            return *value_;
        }

        const E& error() const {
            return *error_;
        }

    
    private:
        std::optional<T> value_;
        std::optional<E> error_;
};