#pragma once

#include <optional>
#include <string>

#include "sipdemo/SipMessage.h"

namespace sipdemo {

// Parse a single SIP datagram into a SipMessage.
//
// UDP gives us exactly one message per datagram, so we parse the whole buffer.
// Returns std::nullopt for malformed input (bad start line, missing CRLFs,
// unterminated headers, Content-Length larger than the body, ...).
std::optional<SipMessage> parseSip(const std::string& raw);

// Convenience helpers for pulling well-known parameters out of header values.

// Extract a ";name=value" parameter (e.g. tag from From/To, branch from Via).
std::optional<std::string> headerParam(const std::string& headerValue,
                                       const std::string& paramName);

// Parse a CSeq header value ("42 INVITE") into its number and method.
struct CSeq {
    long number = 0;
    std::string method;
};
std::optional<CSeq> parseCSeq(const std::string& value);

}  // namespace sipdemo
