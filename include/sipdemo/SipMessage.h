#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sipdemo {

// A parsed SIP message (RFC 3261). One struct covers both requests and
// responses; `isRequest` says which start line is valid.
//
// Headers are stored as an ordered list of (name, value) pairs. Order matters
// on the wire (e.g. multiple Via headers must keep their top-to-bottom order),
// and lookups are case-insensitive per the RFC.
struct SipMessage {
    bool isRequest = true;

    // Request-line fields (valid when isRequest == true).
    std::string method;      // "INVITE", "ACK", "BYE", "OPTIONS", "CANCEL"
    std::string requestUri;  // e.g. "sip:alice@10.0.0.5"

    // Status-line fields (valid when isRequest == false).
    int statusCode = 0;          // 100, 180, 200, 486, ...
    std::string reasonPhrase;    // "OK", "Ringing", ...

    std::string version = "SIP/2.0";

    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    // --- Header access (case-insensitive on name) --------------------------
    std::optional<std::string> get(const std::string& name) const;
    std::vector<std::string> getAll(const std::string& name) const;
    bool has(const std::string& name) const { return get(name).has_value(); }

    // Replace all existing headers of `name` with a single one.
    void set(const std::string& name, const std::string& value);
    // Append a header, preserving any existing ones (e.g. stacked Via).
    void add(const std::string& name, const std::string& value);
    void remove(const std::string& name);

    // Serialize to the wire format. Automatically fixes up Content-Length to
    // match `body` so callers cannot desync it.
    std::string serialize() const;
};

// Case-insensitive ASCII compare of header names / tokens.
bool iequals(const std::string& a, const std::string& b);

}  // namespace sipdemo
