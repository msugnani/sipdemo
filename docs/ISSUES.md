# Issues log

Numbered design / runtime issues we hit while building the demo, how they showed
up, and how they were fixed. Useful interview talking points — each maps to a
real failure mode in SIP/media + AI bridging.

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

### Interview line

> “Real-time media bridges must keep the control channel alive. A send-only WS
> client looks fine in a 30-second smoke test and fails under keepalives.”

---

## ISSUE-002 — Mutex deadlock between `connect()` and the ping reader

| | |
|---|---|
| **Status** | Fixed |
| **Component** | [`src/ws/WsClient.cpp`](../src/ws/WsClient.cpp) |
| **Symptom** | Hang on call setup (or first ping) when starting the WebSocket reader; process appears stuck with media never fully starting. |

### What went wrong

Early draft of `connect()`:

1. Took `mu_`.
2. Completed the HTTP upgrade.
3. Set `connected_ = true`.
4. **Started `readerThread_` while still holding `mu_`.**

The reader, on the first ping, called `sendFrameUnlocked` under `lock_guard(mu_)`
to send a pong → **waited forever** for the lock still held by `connect()`.

```
connect() thread          reader thread
    |                          |
  lock(mu_)                    |
  start reader  -------------> |
  (still holds mu_)         lock(mu_)  ← DEADLOCK
  … never unlocks              |
```

### Fix

Finish the handshake, **release `mu_`**, then start the reader:

```text
handshake under local fd
store fd_ / connected_ under brief lock
unlock
readerThread_ = std::thread(readerLoop)   // may lock for pong safely
```

`close()` sets `readerStop_`, closes the socket (unblocks `recv`), then joins.

### Interview line

> “Never start a thread that needs a mutex you still hold. Lock scope and thread
> lifetime have to be designed together.”

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

### Interview line

> “UDP error models differ by OS. On Windows you must expect connection-reset
> style errors on datagram sockets and never let media threads terminate the
> process.”

---

## How we track these

- Keep this file as the source of truth for demo postmortems.
- Prefer fixing in code + a short entry here over a long GitHub Issues backlog
  for a small interview repo; open GitHub issues if you want CI/discussion.
