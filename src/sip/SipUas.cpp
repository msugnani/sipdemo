#include "sipdemo/SipUas.h"

#include <random>
#include <sstream>

#include "sipdemo/Sdp.h"
#include "sipdemo/SipParser.h"

namespace sipdemo {

namespace {

std::string reasonFor(int code) {
    switch (code) {
        case 100: return "Trying";
        case 180: return "Ringing";
        case 200: return "OK";
        case 400: return "Bad Request";
        case 415: return "Unsupported Media Type";
        case 481: return "Call/Transaction Does Not Exist";
        case 486: return "Busy Here";
        case 487: return "Request Terminated";
        case 488: return "Not Acceptable Here";
        case 500: return "Server Internal Error";
        default: return "OK";
    }
}

}  // namespace

SipUas::SipUas(Config cfg) : cfg_(std::move(cfg)) {}

std::string SipUas::newTag() {
    if (!forcedTag_.empty()) return forcedTag_;
    static std::mt19937 rng(std::random_device{}());
    std::ostringstream os;
    os << "sd" << std::hex << rng() << (++tagCounter_);
    return os.str();
}

// Build a response that correctly echoes the request's transaction/dialog
// identifiers: all Via headers (top-to-bottom), From, To (+ our tag), Call-ID,
// CSeq. Contact and Content-Length are added by callers / serialize().
SipMessage SipUas::makeResponse(const SipMessage& req, int code, const std::string& reason) {
    SipMessage resp;
    resp.isRequest = false;
    resp.statusCode = code;
    resp.reasonPhrase = reason.empty() ? reasonFor(code) : reason;

    for (const auto& via : req.getAll("Via")) resp.add("Via", via);
    if (auto from = req.get("From")) resp.set("From", *from);

    if (auto to = req.get("To")) {
        std::string toVal = *to;
        // Add a To-tag (identifies the dialog's remote end) unless the request
        // already carried one. 100 Trying conventionally carries no tag.
        if (!headerParam(toVal, "tag").has_value() && code > 100) {
            std::string tag = localTag_.empty() ? newTag() : localTag_;
            toVal += ";tag=" + tag;
        }
        resp.set("To", toVal);
    }

    if (auto cid = req.get("Call-ID")) resp.set("Call-ID", *cid);
    if (auto cseq = req.get("CSeq")) resp.set("CSeq", *cseq);
    resp.set("Server", cfg_.userAgent);
    return resp;
}

SipUas::Result SipUas::handle(const SipMessage& in) {
    Result r;
    if (!in.isRequest) return r;  // a UAS ignores stray responses

    const std::string& method = in.method;

    // ---- OPTIONS: keepalive / capability probe, answerable out-of-dialog --
    if (method == "OPTIONS") {
        SipMessage resp = makeResponse(in, 200, "OK");
        resp.set("Allow", "INVITE, ACK, BYE, CANCEL, OPTIONS");
        resp.set("Accept", "application/sdp");
        r.responses.push_back(resp);
        return r;
    }

    // ---- INVITE -----------------------------------------------------------
    if (method == "INVITE") {
        auto cid = in.get("Call-ID");
        auto cseqHdr = in.get("CSeq");
        if (!cid || !cseqHdr) {
            r.responses.push_back(makeResponse(in, 400, ""));
            return r;
        }
        auto cseq = parseCSeq(*cseqHdr);
        long cseqNum = cseq ? cseq->number : -1;

        // Retransmitted INVITE for the call we already answered: just resend the
        // cached 200 OK (RFC 3261 duplicate absorption). Do NOT restart media.
        if (haveDialog_ && *cid == callId_ && cseqNum == inviteCSeq_) {
            if (answered_) r.responses.push_back(last2xxInvite_);
            return r;
        }

        // Busy: we only host one call at a time.
        if (haveDialog_ && *cid != callId_) {
            r.responses.push_back(makeResponse(in, 486, ""));
            return r;
        }

        // New call: set up dialog state.
        haveDialog_ = true;
        answered_ = false;
        callId_ = *cid;
        inviteCSeq_ = cseqNum;
        localTag_ = newTag();
        lastInvite_ = in;

        // Provisional responses.
        r.responses.push_back(makeResponse(in, 100, ""));
        r.responses.push_back(makeResponse(in, 180, ""));

        // Negotiate SDP.
        auto offer = parseSdp(in.body);
        if (!offer) {
            r.responses.push_back(makeResponse(in, 400, "Bad Request (SDP)"));
            haveDialog_ = false;
            return r;
        }
        auto answer = makeAnswer(*offer, cfg_.localIp, cfg_.rtpPort);
        if (!answer) {
            r.responses.push_back(makeResponse(in, 488, ""));
            haveDialog_ = false;
            return r;
        }

        SipMessage ok = makeResponse(in, 200, "OK");
        ok.set("Contact", "<sip:" + cfg_.localIp + ":" + std::to_string(cfg_.sipPort) + ">");
        ok.set("Content-Type", "application/sdp");
        ok.body = answer->generate();

        last2xxInvite_ = ok;
        answered_ = true;
        r.responses.push_back(ok);

        r.media = MediaAction::Start;
        r.remoteRtp = Endpoint{offer->connectionAddress, offer->audioPort};
        r.payloadType = answer->payloadTypes.front();
        r.dtmfPayloadType = answer->dtmfPayloadType;
        return r;
    }

    // ---- ACK: confirms the 2xx; no response is generated ------------------
    if (method == "ACK") {
        return r;
    }

    // ---- CANCEL: cancels an as-yet-unanswered INVITE ----------------------
    if (method == "CANCEL") {
        // 200 OK to the CANCEL transaction itself...
        r.responses.push_back(makeResponse(in, 200, "OK"));
        // ...and 487 to terminate the original INVITE, if it is still pending.
        if (haveDialog_ && in.get("Call-ID") && *in.get("Call-ID") == callId_ && !answered_) {
            r.responses.push_back(makeResponse(lastInvite_, 487, ""));
            haveDialog_ = false;
            r.media = MediaAction::Stop;
        }
        return r;
    }

    // ---- BYE: tears down an established dialog -----------------------------
    if (method == "BYE") {
        if (haveDialog_ && in.get("Call-ID") && *in.get("Call-ID") == callId_) {
            r.responses.push_back(makeResponse(in, 200, "OK"));
            haveDialog_ = false;
            answered_ = false;
            r.media = MediaAction::Stop;
        } else {
            r.responses.push_back(makeResponse(in, 481, ""));
        }
        return r;
    }

    // ---- Anything else -----------------------------------------------------
    SipMessage resp = makeResponse(in, 405, "Method Not Allowed");
    resp.set("Allow", "INVITE, ACK, BYE, CANCEL, OPTIONS");
    r.responses.push_back(resp);
    return r;
}

}  // namespace sipdemo
