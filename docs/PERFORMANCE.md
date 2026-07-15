# Performance & scalability backlog

Engineering notes on where this demo is intentionally simple, and what a
production-grade media path would change. These are **open optimizations**, not
bugs in the current single-call localhost design.

---

## Current baseline (what we ship)

| Area | Today |
|------|--------|
| Concurrency | Mutex + CV `BoundedQueue`; jitter `std::map` |
| Resample | Scalar linear upsample 8→16 kHz |
| Thread priority | Default OS priorities (`std::thread`) |
| UDP I/O | Blocking sockets + `select()` with timeout, then `recvfrom` |
| Scale | One active call; one RTP + one SIP socket |
| Measurement | No dedicated microbench — rely on unit tests + manual demo |

---

## OPT-001 — Use SIMD where it pays off

**Where it applies:** G.711 tables/loops, `upsample8kTo16k`, bulk PCM copies,
checksum-style scans.

**Reality check:** Resampling **is** on the media hot path (every 20 ms frame).
For **one** call, a few hundred `int16` ops are still cheap vs syscalls and
Vosk. SIMD becomes compelling when:

- many concurrent calls share a worker,
- you switch to a heavier resampler (polyphase / sinc),
- or profiling shows the codec/resample leaf dominating.

**Portable options (this repo is C++17 + MSVC on Windows):**

- Compiler auto-vectorization (`-O2` / `RelWithDebInfo`) on tight loops
- Intrinsics or a portable lib (Highway, xsimd)
- `std::experimental::simd` (Parallelism TS) — **GCC/libstdc++**, weak on MSVC
- `std::simd` — **C++26**, not our baseline yet

**Do not** SIMD “everywhere” blindly — only after **OPT-004** shows the leaf.

---

## OPT-002 — Lock-free audio pipeline

**Today:** `BoundedQueue` uses `mutex` + two `condition_variable`s. Fine for
correctness and teaching; under contention it can add latency jitter.

**Target shape (classic media server):**

```text
rtp-recv  --(SPSC lock-free ring)-->  playout/worker
                ^
                |  fixed capacity, drop-oldest or drop-newest policy
```

- **SPSC** (single producer / single consumer) lock-free ring between recv and
  playout — no mutex in the 20 ms path.
- Keep a **bounded** size (backpressure / drop policy) so memory stays finite.
- Pad head/tail indices to avoid **false sharing**.
- Jitter buffer itself may stay lock-free if only the recv thread inserts and
  only playout pops (today’s ownership model already matches that).

**Tradeoff:** Harder to get right (ABA, memory order, shutdown). Mutex queue is
the right teaching default; lock-free is the “scale / latency floor” upgrade.

---

## OPT-003 — Real-time thread priority

**Today:** Default-priority `std::thread`s. On a busy laptop (Zoom + browser),
the 20 ms playout thread can be delayed → underruns / bursty WS sends.

**Upgrade (platform-specific):**

| OS | Approach |
|----|----------|
| Windows | `SetThreadPriority` / multimedia class (`AvSetMmThreadCharacteristics`) |
| Linux | `SCHED_FIFO` / `SCHED_RR` with careful capability (`CAP_SYS_NICE`), or `chrt` |

**Apply to:** playout clock thread first, then RTP recv. **Do not** RT-priority
the Vosk/Python process by default — inference spikes should not starve the
entire machine; isolate AI on normal priority (current process split already
helps).

**Risk:** Priority inversion, starving UI, need for watchdog — document and
gate behind a flag (`--rt-media`).

---

## OPT-004 — Benchmark the hot path

Without numbers, OPT-001–003 are opinions.

**Suggested microbenches (Google Benchmark or a tiny harness):**

1. `g711::decodeMuLaw` / encode — ns per 160-sample frame  
2. `upsample8kTo16k` — ns per frame  
3. `RtpPacket::parse` / `serialize`  
4. `JitterBuffer::insert` + `pop` under reorder  
5. End-to-end `outputLoop` body with WS mocked (null sink)

**Plus:** system traces — Windows ETW / Linux `perf` while on a call; watch
playout wake latency (target ≪ 20 ms).

Gate SIMD / lock-free work on: “this leaf ≥ X% of frame budget.”

---

## OPT-005 — Socket model: blocking, `select`, and why not epoll

### What we do today

[`UdpSocket`](../src/net/UdpSocket.cpp):

1. Socket created in **blocking** mode (default).
2. `recvFrom(timeoutMs)` calls **`select()`** with a timeout (e.g. 200 ms) so
   threads can shut down cleanly.
3. If readable, **`recvfrom()`** reads one datagram.

So:

| Question | Answer |
|----------|--------|
| Blocking or non-blocking? | **Blocking** fd; readiness waited via `select` |
| Edge- vs level-triggered? | **`select` is level-triggered** — stays readable while data remains |
| Multiplexing API | **`select`**, one fd per call in practice |

TCP WebSocket client (`WsClient`) uses blocking `recv`/`send` on its reader /
sender paths (reader thread blocks in `recv`).

### Why `select` (not epoll / IOCP) here

| Reason | Detail |
|--------|--------|
| Portability | Same code on **Windows + Linux**; Windows has no `epoll` |
| Scale of this demo | **O(1)** sockets (SIP + RTP). `select` is fine |
| Simplicity | Timeout + shutdown without a full reactor |

### What production would use

| Platform | Typical choice |
|----------|----------------|
| Linux | `epoll` (often **edge-triggered** + non-blocking + event loop), or `io_uring` |
| Windows | **IOCP** or `WSAPoll` / overlapped I/O |
| Cross-platform | libuv, Asio, etc. |

**Edge-triggered (ET)** vs **level-triggered (LT):**

- **LT** (`select`, default `epoll`): “there is data” — easy; may re-notify until drained.
- **ET** (`epoll` EPOLLET): “state changed to readable” — must drain until `EAGAIN`; fewer wakeups, easier to get wrong.

For many thousand sockets, `select`’s FD_SET size and O(n) scan become the
problem — that’s when epoll/IOCP matter. For one softphone call, they don’t.

---

## Suggested order of attack

1. **OPT-004** measure (always first)  
2. **OPT-003** RT playout if Zoom-class load steals the 20 ms tick  
3. **OPT-002** SPSC ring if mutex shows in profiles under multi-call  
4. **OPT-001** SIMD on proven hot leaves  
5. **OPT-005** reactor/epoll/IOCP when moving beyond a handful of fds  

---

## Related reading in-tree

- [`docs/ISSUES.md`](ISSUES.md) — failures we already fixed (WS drain, lock-across-thread-start, WinUDP)
- [`docs/talking_points.md`](talking_points.md) — component whiteboard notes
