#pragma once

#include <string>
#include "mcp_types.h"
#include <functional>

namespace mcpp {

	class Server; //Forward declaration

	class Server
	{
	public:
		Server(const std::string& name, const std::string& version);
		~Server();

		const std::string& name() const { return name_; }
		const std::string& version() const { return version_; }

	private:

		std::string name_;
		std::string version_;
		bool initialized_;
	};
}