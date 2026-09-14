# mcpp - Model Context Protocol on C++

A cross-platform C++ library for building [Model Context Protocol](https://modelcontextprotocol.io)
servers. It handles the JSON-RPC plumbing and the MCP lifecycle, and lets you add
tools as ordinary C++ functions. RapidJSON is the one dependency, and it's
included here, so you don't need to do anything else after cloning, everything should build.

The repo is the protocol library, nothing else. You can use the source in your own program, register tools, then call `run()`

## How it works

MCP commmunicates either through stdio or http, as of now, this repo only includes the stdio mode.
The LLM host (MCP client) launches your server as a subprocess and exchanges one JSON message per line over stdin/stdout.
The library handles the handshake and routes each incoming request to the right place,
so you only worry about writing the actual tools for your server, everything else gets taken care of by the library.

## Using it

Compile the four `.cpp` files with your program and put the repo root on your
include path, so `mcp_*.h` and `rapidjson/include/...` both resolve.

```cpp
#include "mcp_server.h"

int main() {
    mcp::Server server("my-server", "1.0.0");

    mcp::Tool echo("echo", "Echo the input text back");
    echo.addParameter("text", mcp::PropertyType::String, "Text to echo");

    server.addTool(echo, [](const rapidjson::Value& args) {
        return mcp::ToolResult::text(mcp::args::str(args, "text"));
    });

    return server.run();  // serves over stdio until the client disconnects
}
```

## Build requirements

C++11 or later. The library is single-threaded and cross-platform, the only
platform-specific piece is the Windows binary-mode setup in the stdio transport,
guarded by an `#ifdef`, so the same source builds on Windows, Linux, and macOS.

## License

RapidJSON keeps its own license under `rapidjson/license.txt` (MIT). Only its
headers are vendored, so the JSON License clause that covers the upstream `bin/`
directory doesn't apply here.
