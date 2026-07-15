# sipdemo

Hand-written **C++17** SIP/RTP media endpoint (no PJSIP). Localhost demo: softphone → sipdemo → WebSocket → Python/Vosk STT.

## Components

| Area | Path | Role |
|------|------|------|
| SIP UAS | `src/sip/`, `include/sipdemo/Sip*.h` | INVITE/ACK/BYE/OPTIONS/CANCEL, one call |
| SDP | `src/sip/Sdp.cpp` | G.711 + `telephone-event` offer/answer |
| RTP | `src/rtp/` | Parse/serialize RFC 3550 |
| Jitter buffer | `src/jitter/` | Reorder / late / loss; prime depth 3 (~60 ms) |
| G.711 | `src/codec/G711.*` | µ-law / A-law |
| Resample | `src/codec/Resample.*` | 8 kHz → 16 kHz for STT |
| DTMF | `src/dtmf/Rfc4733.*` | RFC 4733 decoder (press-scoped debounce) |
| WebSocket | `src/ws/WsClient.*` | PCM + DTMF JSON client (ping/pong reader) |
| App | `src/app/main.cpp` | MediaSession threads: recv / playout / output |
| Net | `src/net/UdpSocket.*` | UDP + `select` timeout (Winsock/BSD) |
| Gateway | `gateway/app.py` | FastAPI `/stream` + Vosk |
| Tests | `tests/*.cpp` | GoogleTest via CMake/CTest |
| Scripts | `scripts/build.ps1`, `test.ps1`, `run-demo.ps1` | Windows MSVC workflow |
| Docs | `docs/ISSUES.md`, `PERFORMANCE.md`, `talking_points.md` | Postmortems & backlog |

## Commands (Windows)

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\test.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run-demo.ps1
```

Softphone: dial `sip:127.0.0.1` (MicroSIP: omit `:5060`).

## Constraints

- C++17, CMake ≥ 3.16, single-call UAS, direct IP (no REGISTER).
- Do not commit `build/`, `gateway/.venv/`, or Vosk model binaries.
- Prefer small, focused diffs; verify with `scripts\test.ps1` (exit code 0 only).
