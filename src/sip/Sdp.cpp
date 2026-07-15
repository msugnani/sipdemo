#include "sipdemo/Sdp.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace sipdemo {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Parse "a=rtpmap:<pt> <name>/<rate>[/...]" — returns {pt, name} or nullopt.
std::optional<std::pair<int, std::string>> parseRtpmap(const std::string& val) {
    if (val.compare(0, 7, "rtpmap:") != 0) return std::nullopt;
    auto rest = val.substr(7);
    auto space = rest.find(' ');
    if (space == std::string::npos) return std::nullopt;
    int pt = std::atoi(rest.substr(0, space).c_str());
    auto nameRate = rest.substr(space + 1);
    auto slash = nameRate.find('/');
    std::string name = slash == std::string::npos ? nameRate : nameRate.substr(0, slash);
    return std::make_pair(pt, toLower(name));
}

}  // namespace

bool SdpSession::offersPcmu() const {
    return std::find(payloadTypes.begin(), payloadTypes.end(), 0) != payloadTypes.end();
}

bool SdpSession::offersPcma() const {
    return std::find(payloadTypes.begin(), payloadTypes.end(), 8) != payloadTypes.end();
}

std::string SdpSession::generate() const {
    std::ostringstream os;
    // SDP uses CRLF line endings.
    os << "v=0\r\n";
    os << "o=" << username << " " << sessionId << " " << sessionVersion
       << " IN IP4 " << connectionAddress << "\r\n";
    os << "s=sipdemo\r\n";
    os << "c=IN IP4 " << connectionAddress << "\r\n";
    os << "t=0 0\r\n";

    os << "m=audio " << audioPort << " RTP/AVP";
    for (int pt : payloadTypes) os << " " << pt;
    os << "\r\n";

    // Static payload types still get rtpmap lines for clarity / interop.
    for (int pt : payloadTypes) {
        if (pt == 0) os << "a=rtpmap:0 PCMU/8000\r\n";
        else if (pt == 8) os << "a=rtpmap:8 PCMA/8000\r\n";
        else if (pt == dtmfPayloadType && dtmfPayloadType >= 0) {
            os << "a=rtpmap:" << pt << " telephone-event/8000\r\n";
            os << "a=fmtp:" << pt << " 0-15\r\n";
        }
    }
    os << "a=ptime:" << ptimeMs << "\r\n";
    os << "a=sendrecv\r\n";
    return os.str();
}

std::optional<SdpSession> parseSdp(const std::string& body) {
    SdpSession s;
    bool haveAudio = false;
    bool haveConnection = false;

    std::istringstream iss(body);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.size() < 2 || line[1] != '=') continue;
        char type = line[0];
        std::string val = line.substr(2);

        switch (type) {
            case 'o': {
                auto t = tokenize(val);
                if (t.size() >= 6) {
                    s.username = t[0];
                    s.sessionId = std::strtoull(t[1].c_str(), nullptr, 10);
                    s.sessionVersion = std::strtoull(t[2].c_str(), nullptr, 10);
                    s.connectionAddress = t[5];  // fallback; c= wins below
                }
                break;
            }
            case 'c': {
                // "IN IP4 <addr>"
                auto t = tokenize(val);
                if (t.size() >= 3) {
                    s.connectionAddress = t[2];
                    haveConnection = true;
                }
                break;
            }
            case 'm': {
                // "audio <port> RTP/AVP <pt>..."
                auto t = tokenize(val);
                if (t.size() >= 4 && t[0] == "audio") {
                    s.audioPort = static_cast<uint16_t>(std::atoi(t[1].c_str()));
                    for (size_t i = 3; i < t.size(); ++i) {
                        s.payloadTypes.push_back(std::atoi(t[i].c_str()));
                    }
                    haveAudio = true;
                }
                break;
            }
            case 'a': {
                if (val.compare(0, 6, "ptime:") == 0) {
                    s.ptimeMs = std::atoi(val.c_str() + 6);
                } else if (auto rm = parseRtpmap(val)) {
                    if (rm->second == "telephone-event") {
                        s.dtmfPayloadType = rm->first;
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    if (!haveAudio || s.payloadTypes.empty()) return std::nullopt;
    if (!haveConnection && s.connectionAddress.empty()) return std::nullopt;
    return s;
}

std::optional<SdpSession> makeAnswer(const SdpSession& offer, const std::string& localAddr,
                                     uint16_t localPort) {
    int chosen = -1;
    if (offer.offersPcmu()) chosen = 0;
    else if (offer.offersPcma()) chosen = 8;
    if (chosen < 0) return std::nullopt;  // no common codec

    SdpSession ans;
    ans.username = "sipdemo";
    ans.sessionId = offer.sessionId ? offer.sessionId : 1;
    ans.sessionVersion = 1;
    ans.connectionAddress = localAddr;
    ans.audioPort = localPort;
    ans.payloadTypes = {chosen};
    ans.ptimeMs = offer.ptimeMs > 0 ? offer.ptimeMs : 20;

    // Answer telephone-event when the offer advertised it (preferred PT from
    // offer). Softphones that support RFC 4733 almost always include this.
    if (offer.dtmfPayloadType >= 0) {
        ans.dtmfPayloadType = offer.dtmfPayloadType;
        ans.payloadTypes.push_back(ans.dtmfPayloadType);
    }

    return ans;
}

}  // namespace sipdemo
