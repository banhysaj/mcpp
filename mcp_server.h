#pragma once

#include <string>
#include "mcp_types.h"
#include "mcp_json.h"
#include <functional>

namespace mcpp {

	class Server;    //Forward declaration
	class Transport;

	class RequestContext {
	public:
		std::string method; // the JSON-RPC method being handled
		bool stateless; // true for a stateless (2026-07-28) request
		std::string protocolVersion;
		bool hasProgressToken;

		void notify(const std::string& method, const std::string& rawJsonParams);

		Server* server() const { return server_; }

	private:
		friend class Server;
		RequestContext() : stateless(false), hasProgressToken(false), server_(0) {}
		Server* server_;
	};

	class Server
	{
		friend class RequestContext;

	public:
		Server(const std::string& name, const std::string& version);
		~Server();

		const std::string& name() const { return name_; }
		const std::string& version() const { return version_; }

		int run(); // serve over stdio
		int run(Transport& transport);
		void setTransport(Transport* transport) { transport_ = transport; }
		std::string handleLine(const std::string& line);
		bool initialized() const { return initialized_; }
		void sendNotification(const std::string& method, const std::string& rawJsonParams = std::string("{}"));
		void notifyToolsListChanged();

	private:


		static std::string stripBomAndTrim(const std::string& line);
		std::string name_;
		std::string version_;
		bool initialized_;
		Transport* transport_;

		std::string dispatchOne(const rapidjson::Value& msg);

		std::string onToolsList(const rapidjson::Value& id, bool stateless);
	};
}