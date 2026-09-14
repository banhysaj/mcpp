#include "mcp_server.h"
#include "mcp_constants.h"
#include "mcp_transport.h"

#include <string>
#include <exception>

namespace mcpp {

    using json::addStr;
    using json::addInt;
    using json::addBool;
    using json::addVal;
    using json::addKeyVal;
    using json::pushStr;
    using json::serialize;
    using json::parseInto;

    namespace {
        const char* propTypeName(PropertyType t) {
            switch (t) {
            case PropertyType::String:  return valSchemaString;
            case PropertyType::Number:  return valSchemaNumber;
            case PropertyType::Integer: return valSchemaInteger;
            case PropertyType::Boolean: return valSchemaBoolean;
            case PropertyType::Object:  return valSchemaObject;
            case PropertyType::Array:   return valSchemaArray;
            }
            return valSchemaString;
        }

        // as of now this only applies to latest version
        bool isCacheableMethod(const std::string& m) {
            return m == mtdToolsList || m == mtdResourcesList || m == mtdResourcesTemplatesList ||
                m == mtdResourcesRead || m == mtdPromptsList || m == mtdServerDiscover;
        }

        int logRank(const std::string& lvl) {
            if (lvl == "debug")     return 0;
            if (lvl == "info")      return 1;
            if (lvl == "notice")    return 2;
            if (lvl == "warning")   return 3;
            if (lvl == "error")     return 4;
            if (lvl == "critical")  return 5;
            if (lvl == "alert")     return 6;
            if (lvl == "emergency") return 7;
            return 1;
        }

        rj::Value toolValue(const Tool& t, Alloc& a) {
            rj::Value j(rj::kObjectType);
            addStr(j, fldName, t.name, a);
            if (!t.title.empty()) {
                addStr(j, fldTitle, t.title, a);
            }
            if (!t.description.empty()) {
                addStr(j, fldDescription, t.description, a);
            }

            rj::Value schema;
            if (!t.customInputSchema.empty() && parseInto(t.customInputSchema, schema, a)) {
                addVal(j, fldInputSchema, schema, a);
            }
            else {
                schema.SetObject();
                addStr(schema, fldType, valSchemaObject, a);
                rj::Value props(rj::kObjectType), required(rj::kArrayType);
                for (size_t i = 0; i < t.properties.size(); ++i) {
                    const ToolParameter& p = t.properties[i];
                    rj::Value pj(rj::kObjectType);
                    addStr(pj, fldType, propTypeName(p.type), a);
                    if (!p.description.empty()) {
                        addStr(pj, fldDescription, p.description, a);
                    }
                    if (p.type == PropertyType::Array && !p.itemType.empty()) {
                        rj::Value items(rj::kObjectType);
                        addStr(items, fldType, p.itemType, a);
                        addVal(pj, fldItems, items, a);
                    }
                    if (!p.enumValues.empty()) {
                        rj::Value e(rj::kArrayType);
                        for (size_t k = 0; k < p.enumValues.size(); ++k) pushStr(e, p.enumValues[k], a);
                        addVal(pj, fldEnum, e, a);
                    }
                    addKeyVal(props, p.name, pj, a);
                    if (p.required) {
                        pushStr(required, p.name, a);
                    }
                }
                addVal(schema, fldProperties, props, a);
                if (!required.Empty()) addVal(schema, fldRequired, required, a);
                addVal(j, fldInputSchema, schema, a);
            }

            if (!t.outputSchema.empty()) {
                rj::Value os; if (parseInto(t.outputSchema, os, a)) {
                    addVal(j, fldOutputSchema, os, a);
                }
            }
            return j;
        }

        rj::Value contentValue(const Content& c, Alloc& a) {
            rj::Value j(rj::kObjectType);
            switch (c.kind) {
            case Content::Kind::Text:
                addStr(j, fldType, valTypeText, a);
                addStr(j, fldText, c.text, a);
                break;
            case Content::Kind::Image:
                addStr(j, fldType, valTypeImage, a);
                addStr(j, fldData, c.data, a);
                addStr(j, fldMimeType, c.mimeType, a);
                break;
            case Content::Kind::Audio:
                addStr(j, fldType, valTypeAudio, a);
                addStr(j, fldData, c.data, a);
                addStr(j, fldMimeType, c.mimeType, a);
                break;
            case Content::Kind::ResourceLink:
                addStr(j, fldType, valTypeResourceLink, a);
                addStr(j, fldUri, c.uri, a);
                addStr(j, fldName, c.name, a);
                if (!c.mimeType.empty())    addStr(j, fldMimeType, c.mimeType, a);
                if (!c.description.empty()) addStr(j, fldDescription, c.description, a);
                break;
            case Content::Kind::EmbeddedResource: {
                addStr(j, fldType, valTypeResource, a);
                rj::Value r(rj::kObjectType);
                addStr(r, fldUri, c.uri, a);
                if (!c.mimeType.empty()) addStr(r, fldMimeType, c.mimeType, a);
                if (!c.blob.empty()) addStr(r, fldBlob, c.blob, a);
                else                 addStr(r, fldText, c.text, a);
                addVal(j, fldResource, r, a);
                break;
            }
            }
            return j;
        }

        bool validateToolArgs(const Tool& tool, const rj::Value& args, std::string& err) {
            for (size_t i = 0; i < tool.properties.size(); ++i) {
                const ToolParameter& p = tool.properties[i];
                const rj::Value* v = args::get(args, p.name.c_str());
                if (!v) {
                    if (p.required) { err = "Missing required argument: " + p.name; return false; }
                    continue;
                }
                bool ok = true;
                switch (p.type) {
                case PropertyType::String:  ok = v->IsString(); break;
                case PropertyType::Number:  ok = v->IsNumber(); break;
                case PropertyType::Integer: ok = v->IsInt64() || v->IsUint64(); break;
                case PropertyType::Boolean: ok = v->IsBool();   break;
                case PropertyType::Array:   ok = v->IsArray();  break;
                case PropertyType::Object:  ok = v->IsObject(); break;
                }
                if (!ok) {
                    err = "Argument '" + p.name + "' should be of type " + propTypeName(p.type); return false;
                }
            }
            return true;
        }
    }

    Server::Server(const std::string& name, const std::string& version) : name_(name), version_(version), cacheTtlMs_(0), cacheScope_("private"), initialized_(false), transport_(0) {}
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

    void RequestContext::progress(double progress, double total, const std::string& message) {
        if (!hasProgressToken || !progressToken_ || !server_) return;
        rj::Document d; d.SetObject();
        Alloc& a = d.GetAllocator();
        rj::Value tok; tok.CopyFrom(*progressToken_, a); // echo the client's token
        d.AddMember(rj::StringRef(fldProgressToken), tok, a);
        d.AddMember(rj::StringRef(fldProgress), rj::Value(progress), a);
        if (total >= 0) {
            d.AddMember(rj::StringRef(fldTotal), rj::Value(total), a);
        }
        if (!message.empty()) {
            addStr(d, fldMessage, message, a);
        }
        server_->sendNotificationDoc(ntfProgress, d);
    }

    void Server::addTool(const Tool& tool, ToolHandler handler) {
        ToolEntry e;
        e.tool = tool;
        e.handler = handler;
        tools_.push_back(e);
    }

    void Server::addToolCtx(const Tool& tool, ToolHandlerCtx handler) {
        ToolEntry e;
        e.tool = tool;
        e.handlerCtx = handler;
        e.ctx = true;
        tools_.push_back(e);
    }

    const Server::ToolEntry* Server::findTool(const std::string& name) const {
        for (size_t i = 0; i < tools_.size(); ++i) {
            if (tools_[i].tool.name == name) {
                return &tools_[i];
            }
        }
        return nullptr;
    }

    void Server::addCapabilities(rj::Value& result, Alloc& a) const {
        rj::Value caps(rj::kObjectType);
        if (!tools_.empty()) {
            rj::Value toolsCap(rj::kObjectType);
            addBool(toolsCap, fldListChanged, true, a);
            addVal(caps, fldTools, toolsCap, a);
        }
        addVal(result, fldCapabilities, caps, a);
    }

    // all outbound bytes go through the transport
    void Server::sendLine(const std::string& line) {
        if (transport_) {
            transport_->writeLine(line);
        }
    }

    void Server::sendNotification(const std::string& method, const std::string& rawJsonParams) {
        rj::Document env; env.SetObject();
        Alloc& a = env.GetAllocator();
        addStr(env, fldJsonrpc, kJsonRpcVersion, a);
        addStr(env, fldMethod, method, a);
        if (!rawJsonParams.empty()) {
            rj::Value p; if (parseInto(rawJsonParams, p, a)) {
                env.AddMember(rj::StringRef(fldParams), p, a);
            }
        }
        sendLine(serialize(env));
    }

    void Server::sendNotificationDoc(const std::string & method, rj::Document& params) {
        rj::Document env; env.SetObject();
        Alloc& a = env.GetAllocator();
        addStr(env, fldJsonrpc, kJsonRpcVersion, a);
        addStr(env, fldMethod, method, a);
        rj::Value p; p.CopyFrom(params, a);
        env.AddMember(rj::StringRef(fldParams), p, a);
        sendLine(serialize(env));
    }

    void Server::notifyToolsListChanged() {
        sendNotification(ntfToolsListChanged, "{}");
    }

    void Server::logMessage(const std::string& level, const std::string& message, const std::string& logger) {
        if (!loggingLevel_.empty() && logRank(level) < logRank(loggingLevel_)) return;
        rj::Document d; d.SetObject();
        Alloc& a = d.GetAllocator();
        addStr(d, fldLevel, level, a);
        if (!logger.empty()) {
            addStr(d, fldLogger, logger, a);
        }
        addStr(d, fldData, message, a);
        sendNotificationDoc(ntfMessage, d);
    }

    void RequestContext::log(const std::string& level, const std::string& message) {
        if (server_) server_->logMessage(level, message);
    }

    void RequestContext::notify(const std::string& method, const std::string& rawJsonParams) {
        if (server_) server_->sendNotification(method, rawJsonParams);
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
        if (stateless) {
            addStr(result, fldResultType, kResultTypeComplete, a);
            rj::Value meta(rj::kObjectType);
            rj::Value info;
            buildServerInfo(info, a);
            meta.AddMember(rj::StringRef(kMetaServerInfo), info, a);
            result.AddMember(rj::StringRef(fldMeta), meta, a);

            if (isCacheableMethod(method)) {
                addInt(result, fldTtlMs, cacheTtlMs_, a);
                addStr(result, fldCacheScope, cacheScope_, a);
            }
        }
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

    std::string Server::dispatchOne(const rj::Value& msg) {
        rj::Value nullId;

        if (!msg.IsObject()) {
            return makeError(nullId, kInvalidRequest, "Invalid Request");
        }

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

        // handshake, notifications, and ping are always allowed
        if (method == mtdInitialize)      return onInitialize(id, params);
        if (method == mtdServerDiscover)  return onDiscover(id);
        if (method == ntfInitialized) { initialized_ = true; return ""; }
        if (method == ntfCancelled)       return "";
        if (method == mtdPing) {
            rj::Document empty; empty.SetObject();
            return finalize(id, mtdPing, stateless, empty);
        }

        // Everything else needs a completed handshake. Stateless requests carry
        // their protocol version per-call and are an exception
        if (!stateless && !initialized_) {
            if (!idV) return ""; // notification before init, ignore
            return makeError(id, kInvalidRequest, "Server not initialized");
        }

        if (method == mtdToolsList)       return onToolsList(id, stateless);
        if (method == mtdToolsCall)       return onToolsCall(id, stateless, params);
        if (method == mtdLoggingSetLevel) {
            loggingLevel_ = args::str(params, fldLevel);
            rj::Document empty; empty.SetObject();
            return finalize(id, mtdLoggingSetLevel, stateless, empty);
        }
        if (!idV) return ""; //unknown notification, ignore
        return makeError(id, kMethodNotFound, "Method not found: " + method);
    }

    std::string Server::handleLine(const std::string& line) {
        std::string text = stripBomAndTrim(line);
        if (text.empty()) return "";

        rj::Document msg;
        msg.Parse(text.c_str(), text.size());

        rj::Value nullId;
        if (msg.HasParseError()) return makeError(nullId, kParseError, "Parse error");

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

        if (!instructions_.empty()) {
            addStr(result, fldInstructions, instructions_.c_str(), a);
        }

        initialized_ = true;
        return finalize(id, mtdInitialize, false, result);
    }

    // server/discover
    std::string Server::onDiscover(const rj::Value& id) {
        rj::Document result; result.SetObject();
        Alloc& a = result.GetAllocator();

        rj::Value versions(rj::kArrayType);
        std::vector<std::string> sv = supportedProtocolVersions();
        for (size_t i = 0; i < sv.size(); ++i) pushStr(versions, sv[i], a);
        addVal(result, fldSupportedVersions, versions, a);

        addCapabilities(result, a);

        return finalize(id, mtdServerDiscover, true, result);
    }

    std::string Server::onToolsList(const rj::Value& id, bool stateless) {
        rj::Document result; result.SetObject();
        Alloc& a = result.GetAllocator();
        rj::Value arr(rj::kArrayType);
        for (size_t i = 0; i < tools_.size(); ++i) {
            arr.PushBack(toolValue(tools_[i].tool, a), a);
        }
        addVal(result, fldTools, arr, a);
        return finalize(id, mtdToolsList, stateless, result);
    }

    std::string Server::onToolsCall(const rj::Value& id, bool stateless, const rj::Value& params) {
        std::string name = args::str(params, fldName);
        if (name.empty()) {
            return makeError(id, kInvalidParams, "Missing tool name");
        }
        const ToolEntry* entry = findTool(name);
        if (!entry) {
            return makeError(id, kInvalidParams, "Unknown tool: " + name);
        }

        rj::Value emptyArgs(rj::kObjectType);
        const rj::Value* argsV = args::get(params, fldArguments);
        const rj::Value& toolArgs = argsV ? *argsV : emptyArgs;

        // validate required args plus the loose types before running the handler
        std::string verr;
        if (!validateToolArgs(entry->tool, toolArgs, verr)) {
            return makeError(id, kInvalidParams, verr);
        }

        // the client may attach a progressToken in _meta so the tool can report progress
        const rj::Value* meta = args::get(params, fldMeta);
        const rj::Value* tok = meta ? args::get(*meta, fldProgressToken) : 0;

        // invoke the handler (ctx or plain)
        ToolResult tr;
        try {
            if (entry->ctx) {
                RequestContext ctx;
                ctx.server_ = this;
                ctx.method = mtdToolsCall;
                ctx.stateless = stateless;
                ctx.progressToken_ = tok;
                ctx.hasProgressToken = (tok != 0);
                tr = entry->handlerCtx ? entry->handlerCtx(toolArgs, ctx) : ToolResult::error("Tool has no handler");
            }
            else {
                tr = entry->handler ? entry->handler(toolArgs) : ToolResult::error("Tool has no handler");
            }
        }
        catch (const std::exception& ex) {
            tr = ToolResult::error(std::string("Tool error: ") + ex.what());
        }
        catch (...) {
            tr = ToolResult::error("Tool error: unknown exception");
        }

        rj::Document result; result.SetObject();
        Alloc& a = result.GetAllocator();
        rj::Value content(rj::kArrayType);
        for (size_t i = 0; i < tr.content.size(); ++i) {
            content.PushBack(contentValue(tr.content[i], a), a);
        }
        addVal(result, fldContent, content, a);
        addBool(result, fldIsError, tr.isError, a);
        if (tr.hasStructuredContent && !tr.structuredContent.empty()) {
            rj::Value sc;
            if (parseInto(tr.structuredContent, sc, a)) {
                addVal(result, fldStructuredContent, sc, a);
            }
        }
        return finalize(id, mtdToolsCall, stateless, result);
    }

}
