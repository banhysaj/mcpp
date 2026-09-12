#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace mcp {
	typedef std::string string;
	template<typename T>
	using vector = std::vector<T>;

	enum ErrorCode {
		kParseError = -32700,
		kInvalidRequest = -32600,
		kMethodNotFound = -32601,
		kInvalidParams = -32602,
		kInternalError = -32603,
		kUnsupportedProtocolVersion = -32022
	};

	enum class PropertyType {
		String,
		Number,
		Integer,
		Boolean,
		Object,
		Array
	};

	struct Content {
		enum class Kind { Text, Image, Audio, ResourceLink, EmbeddedResource };

		Kind kind;
		string text;
		string data;
		string blob;
		string mimeType;
		string uri;
		string name;
		string description;

		Content() : kind(Content::Kind::Text) {}
	};

	struct ToolParameter {
		string name;
		PropertyType type;
		string description;
		bool required;
		string itemType;
		vector<string> enumValues;

		ToolParameter() : type(PropertyType::String), required(true) {}
		ToolParameter(const string& n, PropertyType t, const string& d, bool req = true) : name(n), type(t), description(d), required(req) {}
	};

	struct ToolResult {
		vector<Content> content;
		bool isError;
		string structuredContent;
		bool hasStructuredContent;

		ToolResult() : isError(false), hasStructuredContent(false) {}
	};

	struct Tool {
		string name;
		string title;
		string description;
		vector<ToolParameter> properties;
		string customInputSchema;
		string outputSchema;

		Tool() {}
		Tool(const string& n, const string& d) : name(n), description(d) {}
	};

	struct Resource {
		string uri;
		string name;
		string title;
		string description;
		string mimeType;
		int64_t size;

		Resource() : size(0) {}
		Resource(const string& u, const string& n) : uri(u), name(n) {}
	};

	struct ResourceContent {
		string uri;
		string mimeType;
		string text;
		string blob;
		ResourceContent() {}
	};
}