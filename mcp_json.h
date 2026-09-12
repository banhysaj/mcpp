#pragma once

#include <string>
#include <cstdint>

#ifndef RAPIDJSON_HAS_STDSTRING
#define RAPIDJSON_HAS_STDSTRING 1
#endif


#include "rapidjson/include/rapidjson/document.h"

namespace rj = rapidjson;
namespace mcpp {
    namespace args {

        bool has(const rj::Value& v, const char* key);
        const rj::Value* get(const rj::Value& v, const char* key);
        std::string str(const rj::Value& v, const char* key, const std::string& def = {} );
        int64_t integer( const rj::Value& v, const char* key, int64_t def = 0);
        double number(const rj::Value& v, const char* key, double def = 0.0);
        bool boolean(const rj::Value& v, const char* key, bool def = false);
    }
}