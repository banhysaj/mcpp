# mcpp - Model Context Protocol on C++

A cross-platform C++ library for building [Model Context Protocol](https://modelcontextprotocol.io)
servers. It handles the JSON-RPC plumbing and the MCP lifecycle, and lets you add
tools as ordinary C++ functions. RapidJSON is the one dependency, and it's
included here, so you don't need to do anything else after cloning, everything should build.

The repo is the protocol library, nothing else. You can use the source in your own program, register tools, and then call `run()`

## How it works

MCP communicates either through stdio or http, this repo supports both. See [Serving over HTTP](#serving-over-http).
Over stdio, the LLM host (MCP client) launches your server as a subprocess and exchanges one JSON message per line over stdin/stdout.
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

## Asking the user something (elicitation)

Sometimes a tool can't finish with just the arguments it was handed.
Elicitation lets it stop and ask the user a question first.

Since `2026-07-28` no server state is involved. The tool says it needs more
input instead of returning a result, the client asks the user, then calls the
same tool again with the answer attached. So your handler runs twice, and the
second time it can see what the user said.

You need a `RequestContext` for this, so register the tool with `addToolCtx`:

```cpp
mcpp::Tool book("book_table", "Book a table");

server.addToolCtx(book, [](const rapidjson::Value& args,
                           mcpp::RequestContext& ctx) -> mcpp::ToolResult {
    if (!ctx.hasAnswer("guest")) {
        // first run, ask and stash what we'll need on the way back
        mcpp::Elicitation ask = mcpp::Elicitation::makeForm("guest", "Who is the booking for?");
        ask.addField("name", mcpp::PropertyType::String, "Full name");
        ask.addField("seats", mcpp::PropertyType::Integer, "How many seats", false);
        return mcpp::ToolResult::elicit(ask).withRequestState("table-7");
    }

    // second run, the client called back with the answer
    if (!ctx.accepted("guest")) {
        return mcpp::ToolResult::text("No booking made (" + ctx.answerAction("guest") + ").");
    }
    const rapidjson::Value* form = ctx.answerContent("guest");
    if (!form) {
        return mcpp::ToolResult::error("The answer had no form data.");
    }
    return mcpp::ToolResult::text("Booked " + mcpp::args::str(*form, "name")
                                  + " on " + ctx.requestState());
});
```

The fields you add turn into a JSON Schema that the client draws as a form. The
spec only allows flat primitives here, so string, number, integer, boolean and
single-select enums through `setEnum`, no nested objects. If you'd rather write
the schema yourself, `setSchema(rawJson)` replaces the generated one.

Form mode is not allowed for anything sensitive, so no API keys, payments or
signing in to a third party. Those go through URL mode instead, where the user
does it in a browser and the client never sees the data:

```cpp
return mcpp::ToolResult::elicit(
    mcpp::Elicitation::makeUrl("connect", "Connect your account to continue",
                               "https://example.com/connect"))
       .withRequestState(opaqueStateForThisUser);
```

A URL-mode answer has no content, just an action, and `accept` only means the
user agreed to open the link, not that they finished whatever was on the other
side. Check your own records before deciding whether to return a result or ask
again. `makeUrl` fills in a random `elicitationId`. Set your own if you need to
match it against those records.

A few things to keep in mind:

- `requestState` goes out to the client and comes back untouched, so treat it as
  attacker input. Sign or encrypt it if it decides anything about access or
  money, and reject what doesn't verify. The library only carries it.
- Clients say which modes they can handle. Eliciting a mode a client didn't ask
  for fails with `-32021` instead of sending it something it can't draw.
- This is `2026-07-28` only. Older revisions did elicitation a different way that
  isn't supported here, so on a `2025-11-25` or earlier session you get `-32021`.
- Nothing forces the client to come back. It may never call again, and the user
  can decline or cancel, so don't count on the second run happening.

## Serving over HTTP

Add `mcp_http.cpp` to your build and hand the server to an `HttpServer`:

```cpp
#include "mcp_server.h"
#include "mcp_http.h"

int main() {
    mcpp::Server server("my-server", "1.0.0");
    // ... add tools ...

    mcpp::HttpOptions opts;
    opts.host = "0.0.0.0";
    opts.port = 8080;
    opts.authorize = [](const mcpp::HttpRequest& req,
                        std::string& principal, std::string& challenge) {
        if (req.header("Authorization") == "Bearer secret") {
            principal = "user-1";
            return true;
        }
        challenge = "Bearer resource_metadata=\"https://example.com/.well-known/oauth-protected-resource\"";
        return false;
    };

    mcpp::HttpServer http(server, opts);
    return http.listen() ? 0 : 1;
}
```

What it does:

- **One endpoint, POST only.** `opts.path` (default `/mcp`). GET and DELETE get
  405, as the 2026-07-28 spec asks.
- **`tools/call` streams.** When the client accepts `text/event-stream`, progress
  and log lines from `ctx` go out as SSE events before the result. Everything
  else gets a plain JSON reply.
- **One call at a time.** Requests arrive on cpp-httplib's threads, but the
  `Server` and your tools only ever see one at a time, same as over stdio.
- **Headers are checked.** For 2026-07-28 requests, `MCP-Protocol-Version`,
  `Mcp-Method` and `Mcp-Name` must match the body, otherwise the reply is 400
  with `HeaderMismatch` (-32020).
- **Origin is checked.** A request whose `Origin` isn't in `allowedOrigins` gets
  403. With an empty list only localhost origins pass. Requests with no
  `Origin` (server to server) pass.
- **Auth runs first.** `authorize` sees every request before anything is
  parsed. Return true to let it through, and whatever you put in `principal`
  reaches tools as `ctx.principal()`. Return false for a 401 with `challenge`
  as the `WWW-Authenticate` header. Leave it unset for no auth, but then
  anyone who can reach the port can call every tool, so keep `host` at
  `127.0.0.1`.
- **Deeply nested messages are refused.** Anything nested more than 128
  levels gets an error instead of being parsed, on stdio and HTTP alike. The
  parser is recursive, so without this one crafted message could crash the
  server.

Limits:

- **No TLS.** Put a reverse proxy (Caddy, nginx) in front for HTTPS.
- **No server-wide notifications.** `sendNotification` and
  `notifyToolsListChanged` reach stdio clients only, and `subscriptions/listen`
  isn't implemented.
- **Older clients work without sessions.** Clients that start with `initialize`
  are served, but they share one `Server`, so the initialized flag and log
  level are shared too.
- **No cancellation.** A client that hangs up mid-call closes the stream, but
  the tool still runs to the end.

## Build requirements

C++11 or later. The library is single-threaded and cross-platform, the only
platform-specific piece is the Windows binary-mode setup in the stdio transport,
guarded by an `#ifdef`, so the same source builds on Windows, Linux, and macOS.

HTTP adds cpp-httplib (vendored in `httplib/`), which uses threads, so link
pthreads (`-pthread`) on GCC/Clang and `ws2_32.lib` on Windows.

## License

RapidJSON (under `rapidjson/`) and cpp-httplib (under `httplib/`) are third-party and keep their own MIT licenses, please see `rapidjson/license.txt` and `httplib/LICENSE`.
