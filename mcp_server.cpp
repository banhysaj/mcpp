#include "mcp_server.h"
#include "mcp_constants.h"
#include "mcp_transport.h"
#include "rapidjson/include/rapidjson/writer.h"
#include "rapidjson/include/rapidjson/stringbuffer.h"

#include <string>
#include <exception>

namespace mcpp {

    namespace {
        void addStr(rj::Value& o, const char* k, const std::string& s, Alloc& a) {
            rj::Value v(s, a);
            o.AddMember(rj::StringRef(k), v, a);
        }

        void addInt(rj::Value& o, const char* k, long long n, Alloc& a) {
            rj::Value v(static_cast<int64_t>(n)); o.AddMember(rj::StringRef(k), v, a);
        }

        void addBool(rj::Value& o, const char* k, bool b, Alloc& a) {
            rj::Value v(b); o.AddMember(rj::StringRef(k), v, a);
        }

        void addVal(rj::Value& o, const char* k, rj::Value& v, Alloc& a) {
            o.AddMember(rj::StringRef(k), v, a);   // moves v
        }

        std::string serialize(const rj::Value& v) {
            rj::StringBuffer sb;
            rj::Writer<rj::StringBuffer> w(sb);
            v.Accept(w);
            return std::string(sb.GetString(), sb.GetSize());
        }
    }

    Server::Server(const std::string& name, const std::string& version) : name_(name), version_(version), initialized_(false), transport_(0) {}
    Server::~Server() {}

    int Server::run() {
        StdioTransport transport;
        return run(transport);
    }

    int Server::run(Transport& transport) {
        transport_ = &transport;
        std::string line;
        while (transport.readLine(line)) {
            std::string resp = handleLine(line);
            if (!resp.empty()) {
                transport.writeLine(resp);
            }
        }
        transport_ = 0;
        return 0;
    }

    void Server::buildServerInfo(rj::Value& out, Alloc& a) const {
        out.SetObject();
        addStr(out, fldName, name_, a);
        addStr(out, fldVersion, version_, a);
    }

    void Server::addCapabilities(rj::Value& result, Alloc& a) const {
        rj::Value caps(rj::kObjectType);
        addVal(result, fldCapabilities, caps, a); //none for now
    }

    std::string Server::stripBomAndTrim(const std::string& line) {
        size_t b = 0;
        size_t e = line.size();
        if (e - b >= 3 && (unsigned char)line[b] == 0xEF && (unsigned char)line[b + 1] == 0xBB && (unsigned char)line[b + 2] == 0xBF) {
            b += 3;
        }
        while (b < e && (line[b] == ' ' || line[b] == '\t' || line[b] == '\r' || line[b] == '\n')) {
            ++b;
        }
        while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r' || line[e - 1] == '\n')) {
            --e;
        }
        return line.substr(b, e - b);
    }

    std::string Server::finalize(const rj::Value& id, const char* method, bool stateless, rj::Document& resultDoc) {
        rj::Document env;
        env.SetObject();
        Alloc& a = env.GetAllocator();
        addStr(env, fldJsonrpc, kJsonRpcVersion, a);
        rj::Value idCopy;
        idCopy.CopyFrom(id, a);
        env.AddMember(rj::StringRef(fldId), idCopy, a);

        rj::Value result;
        result.CopyFrom(resultDoc, a);
        env.AddMember(rj::StringRef(fldResult), result, a);
        return serialize(env);
    }

    std::string Server::makeError(const rj::Value& id, int code, const  std::string& message) {
        rj::Document env; env.SetObject();
        Alloc& a = env.GetAllocator();
        addStr(env, fldJsonrpc, kJsonRpcVersion, a);
        rj::Value idCopy; idCopy.CopyFrom(id, a);
        env.AddMember(rj::StringRef(fldId), idCopy, a);
        rj::Value err(rj::kObjectType);
        addInt(err, fldCode, code, a);
        addStr(err, fldMessage, message, a);
        env.AddMember(rj::StringRef(fldError), err, a);
        return serialize(env);
    }

    // initialize, echo the client's version if we support it.
    std::string Server::onInitialize(const rj::Value& id, const rj::Value& params) {
        rj::Document result; result.SetObject();
        Alloc& a = result.GetAllocator();

        std::string requested = args::str(params, fldProtocolVersion);
        bool supported = isStatefulVersion(requested) || isStatelessVersion(requested);
        addStr(result, fldProtocolVersion, supported ? requested.c_str() : defaultStatefulVersion(), a);

        addCapabilities(result, a);

        rj::Value info; buildServerInfo(info, a);
        addVal(result, fldServerInfo, info, a);

        initialized_ = true;
        return finalize(id, mtdInitialize, false, result);
    }

    std::string Server::dispatchOne(const rj::Value& msg) {
        rj::Value nullId;

        const rj::Value* methodV = args::get(msg, fldMethod);
        const rj::Value* idV = args::get(msg, fldId);
        const rj::Value& id = idV ? *idV : nullId;

        if (!methodV || !methodV->IsString()) {
            if (!idV) return "";  // notification with no valid method, just ignore
            return makeError(id, kInvalidRequest, "Invalid Request");
        }
        std::string method = methodV->GetString();

        rj::Value emptyObj(rj::kObjectType);
        const rj::Value* paramsV = args::get(msg, fldParams);
        const rj::Value& params = paramsV ? *paramsV : emptyObj;

        const rj::Value* meta = args::get(params, fldMeta);
        const rj::Value* pv = meta ? args::get(*meta, kMetaProtocolVersion) : 0;
        bool stateless = (pv && pv->IsString());

        if (method == mtdInitialize)      return onInitialize(id, params);

        return makeError(id, kMethodNotFound, "Method not found: " + method);
    }

    std::string Server::handleLine(const std::string& line) {
        std::string text = stripBomAndTrim(line);
        if (text.empty()) return "";

        rj::Document msg;
        msg.Parse(text.c_str(), text.size());

        if (msg.IsArray()) {
            std::string out;
            for (rj::SizeType i = 0; i < msg.Size(); ++i) {
                std::string r = dispatchOne(msg[i]);
                if (r.empty()) continue;
                out += out.empty() ? "[" : ","; //If there's at least one reply, open OR keep adding elements in the array, separated by ,
                out += r;
            }
            if (!out.empty()) out += "]"; //close
            return out;
        }
        return dispatchOne(msg);
    }
}
