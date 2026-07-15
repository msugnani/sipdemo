# Issues log

Numbered design / runtime issues encountered while building the demo, how they
showed up, and how they were fixed. Each maps to a real failure mode in
SIP/media + AI bridging.

---

## ISSUE-001 — WebSocket receive buffer never drained (STT dies mid-call)

| | |
|---|---|
| **Status** | Fixed |
| **Component** | [`src/ws/WsClient.cpp`](../src/ws/WsClient.cpp) |
| **Symptom** | Echo and SIP keep working, but STT stops after a while. Gateway prints `[STT FINAL]: …` while the call is still up. |

### What went wrong

`WsClient` was **send-only**: it pushed PCM/DTMF frames to
`ws://127.0.0.1:8080/stream` but never read the socket.

Uvicorn/Starlette periodically sends WebSocket **ping** frames. With no reader:

1. Ping data piles up in the TCP receive buffer.
2. The window fills / the peer eventually closes or sends fail.
3. `sendFrame` fails → we marked the socket closed.
4. Further audio is dropped; the gateway `finally` block runs `FinalResult()` →
   `[STT FINAL]`.

Classic “half-duplex WebSocket client” footgun.

### Fix

After a successful handshake, spawn a **reader thread** that:

- drains server→client frames,
- answers **ping (opcode 0x9)** with **pong (opcode 0xA)** (masked, as required
  for client frames),
- treats **close (0x8)** as end-of-session,
- ignores text/binary from the server (demo doesn’t need them).

---

## ISSUE-002 — Holding `mu_` across reader-thread start

| | |
|---|---|
| **Status** | Fixed |
| **Component** | [`src/ws/WsClient.cpp`](../src/ws/WsClient.cpp) |
| **Symptom** | Not a live demo hang — caught while adding the ISSUE-001 reader. TCP/`::connect` and the HTTP upgrade were fine either way. |

### What went wrong

Early draft published state and started the reader under one `lock_guard(mu_)`:

1. TCP connect + HTTP WebSocket upgrade on a **local** `fd` (no mutex needed).
2. Under `mu_`: set `fd_` / `connected_ = true`, then **`std::thread(readerLoop)`**.
3. Unlock only when that scope ended.

The reader answers ping→pong by locking `mu_` again. Spawning it while still
holding the lock is a classic anti-pattern:

- **Usually** not a permanent hang — `connect()` unlocks soon after the thread
  constructor returns; the reader may only stall briefly if a ping races that
  window.
- **True deadlock** if `connect()` (or `close()`) also **waits on the reader**
  (`join`, barrier, etc.) while still holding `mu_`, because the reader needs
  the same mutex for pong / teardown.

So this was a lock-ordering / lifecycle bug class, not “`connect()` couldn’t
complete the handshake.”

```
connect() thread          reader thread
    |                          |
  lock(mu_)                    |
  start reader  -------------> |
  (still holds mu_)         lock(mu_)  ← blocks until unlock
  unlock                       |       (or deadlock if connect also joins here)
```

### Fix

Handshake on a local `fd`, publish under a **brief** lock, **then** start the
reader:

```text
handshake under local fd
store fd_ / connected_ under brief lock
unlock
readerThread_ = std::thread(readerLoop)   // may lock for pong safely
```

`close()` sets `readerStop_`, closes the socket (unblocks `recv`), then joins
**without** holding `mu_` across the join.

---

## ISSUE-003 — Windows UDP `WSAECONNRESET` aborts the media thread

| | |
|---|---|
| **Status** | Fixed |
| **Component** | [`src/net/UdpSocket.cpp`](../src/net/UdpSocket.cpp), [`src/app/main.cpp`](../src/app/main.cpp) |
| **Symptom** | Process crash / abrupt exit during a call when echoing to a peer that isn’t listening (or after ICMP port unreachable). |

### What went wrong

On Windows, `sendto` to an unbound UDP port can generate ICMP port-unreachable.
Winsock surfaces that on the **next** `recvfrom` of the **same** socket as
`WSAECONNRESET` (10054). Our `recvFrom` threw; an uncaught exception on a
`std::thread` calls `std::terminate` → process dies. Echo path still looked
“correct” on paper; the softphone smoke test without a listener on the remote
RTP port triggered it easily.

### Fix

1. Treat `WSAECONNRESET` / `WSAECONNREFUSED` / `WSAENETRESET` on `recvfrom` as
   soft misses (`nullopt`), not hard errors.
2. Wrap media worker loops in `try/catch` so one socket glitch can’t kill the
   process.
3. **Symmetric RTP latch**: send echo to the source of incoming RTP, not only
   the SDP `c=`/`m=` address (separately improved echo reliability with
   MicroSIP).

---

## How we track these

- Keep this file as the source of truth for demo postmortems.
- Prefer fixing in code + a short entry here over a long GitHub Issues backlog
  for a small repo; open GitHub issues if you want CI/discussion.
