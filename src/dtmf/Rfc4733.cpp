#include "sipdemo/Rfc4733.h"

namespace sipdemo {

std::optional<DtmfEvent> parseDtmfPayload(const uint8_t* data, size_t len) {
    if (!data || len < 4) return std::nullopt;
    DtmfEvent e;
    e.event = data[0];
    e.end = (data[1] & 0x80) != 0;
    e.volume = static_cast<uint8_t>(data[1] & 0x3f);
    e.duration = static_cast<uint16_t>((static_cast<uint16_t>(data[2]) << 8) | data[3]);
    return e;
}

std::optional<char> dtmfEventToChar(uint8_t event) {
    if (event <= 9) return static_cast<char>('0' + event);
    if (event == 10) return '*';
    if (event == 11) return '#';
    if (event >= 12 && event <= 15) return static_cast<char>('A' + (event - 12));
    return std::nullopt;
}

std::optional<char> DtmfDecoder::feed(const std::vector<uint8_t>& payload) {
    auto ev = parseDtmfPayload(payload);
    if (!ev) return std::nullopt;

    if (static_cast<int>(ev->event) != lastEvent_) {
        lastEvent_ = ev->event;
        emittedForCurrent_ = false;
        lastDuration_ = 0;
    }

    // Non-end packet = key is down. Clears the "already emitted" latch so the
    // same digit can fire again on the next release (previous bug: same key
    // only worked once per call).
    if (!ev->end) {
        emittedForCurrent_ = false;
        lastDuration_ = ev->duration;
        return std::nullopt;
    }

    // End packet: duration restart means a new press that we only saw as ends
    // (some softphones omit intermediate !end packets between presses).
    if (lastDuration_ > 0 && ev->duration < lastDuration_) {
        emittedForCurrent_ = false;
    }
    lastDuration_ = ev->duration;

    if (emittedForCurrent_) return std::nullopt;  // end retransmit

    auto ch = dtmfEventToChar(ev->event);
    if (!ch) return std::nullopt;
    emittedForCurrent_ = true;
    return ch;
}

void DtmfDecoder::reset() {
    lastEvent_ = -1;
    emittedForCurrent_ = false;
    lastDuration_ = 0;
}

}  // namespace sipdemo
