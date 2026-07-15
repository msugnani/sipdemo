# Interview talking points

A cheat-sheet mapping each component of this project to the questions it lets
you answer. Everything here is implemented by hand in this repo, so you can
whiteboard any of it.

## SIP signaling (`sip/`)

- **Transaction vs. dialog.** A *transaction* is one request + its responses,
  matched by the Via `branch` (magic cookie `z9hG4bK`), sent-by, and CSeq
  method. A *dialog* is the longer-lived peer relationship identified by
  `Call-ID` + local tag + remote tag. INVITE creates a dialog; BYE ends it.
- **UAS vs. UAC.** We are the server side (User Agent Server): we receive
  requests and generate responses. A UAC originates requests.
- **Response construction.** A response echoes the request's Via stack (in
  order), From, To (we add our `tag`), Call-ID and CSeq. 2xx to INVITE adds a
  `Contact` so in-dialog requests (BYE) can be routed back.
- **Why UDP + text.** SIP is human-readable text (easy to debug in Wireshark)
  and usually rides UDP; reliability is provided by SIP's own retransmission
  timers rather than TCP. We absorb duplicate INVITE/ACK instead of building the
  full Timer A–K machinery — know the names to discuss the tradeoff.
- **INVITE flow.** INVITE → 100 Trying → 180 Ringing → 200 OK (carries SDP
  answer) → ACK. BYE → 200. CANCEL cancels a not-yet-answered INVITE (200 to the
  CANCEL, 487 to the INVITE). See `SipUas::handle`.

## SDP offer/answer (`sip/Sdp.*`)

- Offer/answer model (RFC 3264): the caller's INVITE body is the **offer**; our
  200 OK body is the **answer**. We pick a codec we support (PCMU preferred,
  then PCMA), echo `ptime`, and advertise our own `c=` address and `m=audio`
  port.
- Static payload types: 0 = PCMU, 8 = PCMA, both 8 kHz (RFC 3551).

## RTP (`rtp/RtpPacket.*`)

- Fixed 12-byte header: version, padding/extension/CC flags, marker, payload
  type, 16-bit **sequence**, 32-bit **timestamp**, **SSRC**, optional CSRCs.
- **Sequence wraparound.** Sequence numbers are 16-bit and wrap; comparisons use
  RFC 1982 serial-number arithmetic (`seqLess`), not naive `<`. The jitter
  buffer and stats unwrap to 64-bit to stay correct across the boundary.
- Network byte order everywhere; bit fields packed by hand.

## Jitter buffer (`jitter/JitterBuffer.*`)

- Reorders by (unwrapped) sequence number, detects **late** (arrived after its
  slot — discard) and **lost** (gap declared once a later packet is available —
  play silence/PLC) packets.
- **Jitter vs. latency tradeoff.** A deeper buffer smooths more reordering/jitter
  at the cost of end-to-end latency. `targetDepth` is the knob; we prime to it
  before starting playout.

## Concurrency (`concurrency/BoundedQueue.h`)

- Fixed-capacity, thread-safe queue with **blocking** (`push`/`pop`, backpressure)
  and **non-blocking** (`tryPush`/`tryPop`) variants; `close()` drains then wakes
  waiters for clean shutdown.
- **Lock-based vs. lock-free.** This uses a mutex + two condition variables. For
  a single-producer/single-consumer audio handoff a lock-free SPSC ring buffer
  would avoid contention; mention **false sharing** (pad head/tail to cache
  lines) and why backpressure matters (bounded memory, drop policy).
- Validated under **ThreadSanitizer** (`-DSIPDEMO_SANITIZE_THREAD=ON`).

## G.711 codec (`codec/G711.*`)

- µ-law and A-law companding: 16-bit linear PCM ↔ 8-bit logarithmic code words.
  Companding gives better SNR at low amplitudes than linear 8-bit. Round-trip is
  lossy but bounded by the quantization step at each amplitude.

## RtpStats (`stats/RtpStats.*`)

- Loss = expected − received across the observed sequence range; reorder count;
  RFC 3550 **interarrival jitter** as a first-order low-pass (gain 1/16) of the
  transit-time differences, in RTP timestamp units (and ms).

## C++ craft to point at

- **RAII / ownership.** `UdpSocket` owns the OS handle, non-copyable,
  move-only; Winsock init via a function-local static guard.
- **Move semantics.** Packets/payloads are `std::move`d through the pipeline to
  avoid copies; `BoundedQueue<T>` takes `T` by value and moves internally.
- **Value-oriented, testable design.** `SipUas::handle` is a pure-ish function
  (message in → responses + media event out), so the whole state machine is
  unit-tested with scripted sequences and no sockets.

## Real-time media vs AI latency

- The RTP recv / playout / echo path never waits on Vosk. Decoded PCM is
  forked over a WebSocket; if the gateway is down, the call and echo continue.
- **Bounded queue + tryPush** is the backpressure story: under overload we drop
  frames rather than unbounded memory growth. A lock-free SPSC ring would be
  the next step for a production media thread.
- **G.711 @ 8 kHz → linear16 @ 16 kHz.** Telephony audio is upsampled before
  STT because Vosk (and most ASR) expects 16 kHz PCM. Mention quantization /
  sample-rate mismatch as a classic integration footgun.

## DTMF out-of-band vs ASR

- Softphones send keypad presses as **RFC 4733 telephone-event** RTP (dynamic
  PT, usually 101), not as audio tones in the voice channel.
- We negotiate `a=rtpmap:… telephone-event/8000` in SDP, branch those packets
  out of the jitter/echo path, and emit JSON `{"type":"dtmf","digit":"5"}` to
  the AI controller — instant, deterministic, no STT involved.
- Interview line: “voice for NLU, DTMF for structured input (IVR menus, PIN).”

## WSL / SDP reachability

- SDP `c=` and `m=` advertise where the peer should send RTP. Under WSL2 NAT,
  that must be the **WSL eth0 IP**, not `127.0.0.1`, or the Windows softphone
  sends media to the wrong host. Same principle as advertising a public IP
  behind NAT in production.

