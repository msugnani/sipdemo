#include "sipdemo/SipParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace sipdemo {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Split "SIP/2.0" style start lines and headers on the first N delimiters.
std::vector<std::string> splitLines(const std::string& raw, size_t headerEnd) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < headerEnd) {
        size_t nl = raw.find("\r\n", pos);
        if (nl == std::string::npos || nl > headerEnd) nl = headerEnd;
        lines.push_back(raw.substr(pos, nl - pos));
        pos = nl + 2;
    }
    return lines;
}

}  // namespace

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

std::optional<std::string> SipMessage::get(const std::string& name) const {
    for (const auto& h : headers) {
        if (iequals(h.first, name)) return h.second;
    }
    return std::nullopt;
}

std::vector<std::string> SipMessage::getAll(const std::string& name) const {
    std::vector<std::string> out;
    for (const auto& h : headers) {
        if (iequals(h.first, name)) out.push_back(h.second);
    }
    return out;
}

void SipMessage::set(const std::string& name, const std::string& value) {
    remove(name);
    headers.emplace_back(name, value);
}

void SipMessage::add(const std::string& name, const std::string& value) {
    headers.emplace_back(name, value);
}

void SipMessage::remove(const std::string& name) {
    headers.erase(std::remove_if(headers.begin(), headers.end(),
                                 [&](const auto& h) { return iequals(h.first, name); }),
                  headers.end());
}

std::string SipMessage::serialize() const {
    std::string out;
    if (isRequest) {
        out += method + " " + requestUri + " " + version + "\r\n";
    } else {
        out += version + " " + std::to_string(statusCode) + " " + reasonPhrase + "\r\n";
    }
    for (const auto& h : headers) {
        if (iequals(h.first, "Content-Length")) continue;  // authoritative value below
        out += h.first + ": " + h.second + "\r\n";
    }
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += "\r\n";
    out += body;
    return out;
}

std::optional<SipMessage> parseSip(const std::string& raw) {
    // Header section ends at the first blank line (CRLF CRLF).
    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return std::nullopt;

    std::vector<std::string> lines = splitLines(raw, headerEnd);
    if (lines.empty() || lines[0].empty()) return std::nullopt;

    SipMessage msg;

    // --- Start line --------------------------------------------------------
    const std::string& start = lines[0];
    if (start.compare(0, 4, "SIP/") == 0) {
        // Status line: "SIP/2.0 200 OK"
        msg.isRequest = false;
        size_t sp1 = start.find(' ');
        if (sp1 == std::string::npos) return std::nullopt;
        size_t sp2 = start.find(' ', sp1 + 1);
        msg.version = start.substr(0, sp1);
        std::string code = start.substr(sp1 + 1, (sp2 == std::string::npos ? start.size() : sp2) - sp1 - 1);
        if (code.size() != 3 || !std::all_of(code.begin(), code.end(), ::isdigit)) return std::nullopt;
        msg.statusCode = std::atoi(code.c_str());
        msg.reasonPhrase = (sp2 == std::string::npos) ? "" : trim(start.substr(sp2 + 1));
    } else {
        // Request line: "INVITE sip:bob@host SIP/2.0"
        msg.isRequest = true;
        size_t sp1 = start.find(' ');
        if (sp1 == std::string::npos) return std::nullopt;
        size_t sp2 = start.rfind(' ');
        if (sp2 == sp1) return std::nullopt;
        msg.method = start.substr(0, sp1);
        msg.requestUri = trim(start.substr(sp1 + 1, sp2 - sp1 - 1));
        msg.version = trim(start.substr(sp2 + 1));
        if (msg.method.empty() || msg.requestUri.empty()) return std::nullopt;
        if (msg.version.compare(0, 4, "SIP/") != 0) return std::nullopt;
    }

    // --- Headers (with line-folding continuation support) ------------------
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty()) return std::nullopt;  // stray blank line inside headers

        // A line starting with SP/HTAB is a folded continuation of the prev header.
        if (line[0] == ' ' || line[0] == '\t') {
            if (msg.headers.empty()) return std::nullopt;
            msg.headers.back().second += " " + trim(line);
            continue;
        }

        size_t colon = line.find(':');
        if (colon == std::string::npos) return std::nullopt;
        std::string name = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        if (name.empty()) return std::nullopt;
        msg.headers.emplace_back(name, value);
    }

    // --- Body (validated against Content-Length) ---------------------------
    std::string body = raw.substr(headerEnd + 4);
    if (auto cl = msg.get("Content-Length")) {
        char* end = nullptr;
        long declared = std::strtol(cl->c_str(), &end, 10);
        if (end == cl->c_str() || declared < 0) return std::nullopt;
        if (static_cast<size_t>(declared) > body.size()) return std::nullopt;  // truncated
        body.resize(static_cast<size_t>(declared));
    }
    msg.body = std::move(body);

    return msg;
}

std::optional<std::string> headerParam(const std::string& headerValue,
                                       const std::string& paramName) {
    // Scan for ";param" honoring the token boundary so "tag" != "xtag".
    size_t pos = 0;
    while ((pos = headerValue.find(';', pos)) != std::string::npos) {
        size_t start = pos + 1;
        size_t eq = headerValue.find('=', start);
        size_t semi = headerValue.find(';', start);
        size_t nameEnd = std::min(eq, semi);
        if (nameEnd == std::string::npos) nameEnd = headerValue.size();
        std::string name = trim(headerValue.substr(start, nameEnd - start));
        if (iequals(name, paramName)) {
            if (eq == std::string::npos || (semi != std::string::npos && semi < eq)) {
                return std::string();  // flag parameter, present but valueless
            }
            size_t valEnd = headerValue.find(';', eq + 1);
            if (valEnd == std::string::npos) valEnd = headerValue.size();
            return trim(headerValue.substr(eq + 1, valEnd - eq - 1));
        }
        pos = start;
    }
    return std::nullopt;
}

std::optional<CSeq> parseCSeq(const std::string& value) {
    std::string v = trim(value);
    size_t sp = v.find(' ');
    if (sp == std::string::npos) return std::nullopt;
    CSeq c;
    char* end = nullptr;
    c.number = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str()) return std::nullopt;
    c.method = trim(v.substr(sp + 1));
    if (c.method.empty()) return std::nullopt;
    return c;
}

}  // namespace sipdemo
