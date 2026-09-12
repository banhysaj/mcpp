#include "mcp_json.h"

namespace mcpp {
    namespace {
        const rj::Value* member(const rj::Value& v, const char* k) {
            if (!v.IsObject()) return 0;
            rj::Value::ConstMemberIterator it = v.FindMember(k);
            if (it == v.MemberEnd()) return 0;
            return &it->value;
        }
    }

    namespace args {
        bool has(const rj::Value& v, const char* key) {
            return member(v, key) != 0;
        }

        const rj::Value* get(const rj::Value& v, const char* key) { 
            return member(v, key); 
        }

        std::string str(const rj::Value& v, const char* key, const std::string& def) {
            const rj::Value* m = member(v, key);
            if (m && m->IsString()) {
                return std::string(m->GetString(), m->GetStringLength());
            }
            return def;
        }
        int64_t integer(const rj::Value& v, const char* key, int64_t def) {
            const rj::Value* m = member(v, key);
            if (!m) return def;
            if (m->IsInt64())  return m->GetInt64();
            if (m->IsUint64()) return static_cast<int64_t>(m->GetUint64());
            if (m->IsDouble()) return static_cast<int64_t>(m->GetDouble());
            return def;
        }
        double number(const rj::Value& v, const char* key, double def) {
            const rj::Value* m = member(v, key);
            if (m && m->IsNumber()) {
                return m->GetDouble();
            }
            return def;
        }
        bool boolean(const rj::Value& v, const char* key, bool def) {
            const rj::Value* m = member(v, key);
            if (m && m->IsBool()) {
                return m->GetBool();
            }
            return def;
        }
        
    }
}