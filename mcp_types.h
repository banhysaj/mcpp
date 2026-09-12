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

		static Content makeText(const std::string& t);
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

		static ToolResult text(const std::string& t);   // A single text block
		static ToolResult error(const std::string& message);
		ToolResult& add(const Content& c) {
			content.push_back(c); return *this;
		}
		ToolResult& addText(const std::string& t) {
			content.push_back(Content::makeText(t));
			return *this;
		}
		ToolResult& withStructured(const std::string& rawJson) {
			structuredContent = rawJson; hasStructuredContent = true;
			return *this;
		}
		ToolResult& markError(bool e = true) {
			isError = e;
			return *this;
		}
	};

	struct Tool {
		string name;
		string title;
		string description;
		vector<ToolParameter> properties;
		string customInputSchema; //When set, custom json overrides the generated schema
		string outputSchema; //Optional raw json

		Tool() {}
		Tool(const string& n, const string& d) : name(n), description(d) {}
	};

	struct Resource {
		string uri;
		string name;
		string title;
		string description;
		string mimeType;
		int64_t size; //Size in bytes, optional

		Resource() : size(0) {}
		Resource(const string& u, const string& n) : uri(u), name(n) {}
	};

	struct ResourceContent {
		string uri;
		string mimeType;
		string text; //If non-empty, and blob is empty, sent as text
		string blob; //Else, sent as base64 "blob"
		ResourceContent() {}
	};
}