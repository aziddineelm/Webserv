# Phase 2 Concepts — Everything You Need to Learn

> You already know the syscalls individually. Phase 2 is about **orchestrating them into an event-driven machine**. These are the concepts that glue everything together.

---

## Concept 1: The Event-Driven Model (vs. Thread-per-Client)

### What Is It?
Instead of spawning a thread/process for each client (like Apache), you use **one single loop** that handles ALL clients by checking which FDs are ready.

### The Mental Model

```
Thread-per-client (NOT what you're doing):
┌──────────┐  ┌──────────┐  ┌──────────┐
│ Thread 1 │  │ Thread 2 │  │ Thread 3 │
│ Client A │  │ Client B │  │ Client C │
│ recv()   │  │ recv()   │  │ recv()   │  ← each thread BLOCKS on one client
│ ...wait..│  │ ...wait..│  │ ...wait..│
└──────────┘  └──────────┘  └──────────┘

Event-driven (YOUR model):
┌─────────────────────────────────────────┐
│           Single Thread / Loop          │
│                                         │
│  poll() → "Client A has data"           │
│         → recv(A) → got 100 bytes       │
│         → "Client C has data"           │
│         → recv(C) → got 200 bytes       │
│         → "Client B can write"          │
│         → send(B) → sent response       │
│                                         │
│  poll() → next round...                 │
└─────────────────────────────────────────┘
```

### Why This Matters for You
- **One poll() call** watches ALL FDs simultaneously (listeners + clients + future CGI pipes)
- You never block on a single client — if one is slow, you serve others
- The 42 subject **mandates** this: "only 1 poll() for all I/O operations"

### What You Need to Understand
- Your server is **single-threaded** — there's no parallelism, just fast switching
- Every operation must be **non-blocking** — if `recv()` would block, it returns EAGAIN
- You handle events **reactively**: "poll told me this FD is ready, so NOW I act on it"

---

## Concept 2: The pollfd Array — Your FD Registry

### What Is It?
`poll()` takes an array of `struct pollfd`. This array is your **registry of everything the server is watching**.

### The Structure
```cpp
struct pollfd {
    int   fd;       // The file descriptor to watch
    short events;   // What you're interested in (POLLIN, POLLOUT)
    short revents;  // What actually happened (filled by kernel)
};
```

### The Dynamic Array Problem

Your `pollfd` array changes constantly:
```
Server starts:     [listen:8080, listen:8081]                    → 2 entries
Client connects:   [listen:8080, listen:8081, client_1]          → 3 entries
Another connects:  [listen:8080, listen:8081, client_1, client_2] → 4 entries
Client_1 done:     [listen:8080, listen:8081, client_2]          → 3 entries
CGI starts:        [listen:8080, listen:8081, client_2, cgi_pipe] → 4 entries
```

### What You Need to Understand
- You'll use `std::vector<struct pollfd>` (dynamic sizing)
- Adding an FD = `push_back()` a new pollfd entry
- Removing an FD = swap with last element + `pop_back()` (avoids shifting)
- **Never modify the vector WHILE iterating** — collect changes, apply after the loop
- The pollfd array and your Client map must stay **in sync**

### The Sync Problem — This Is Where Bugs Live
```
You have:
  pollfds[0] = listen_8080     clients[fd=3]  → type: LISTEN
  pollfds[1] = listen_8081     clients[fd=4]  → type: LISTEN
  pollfds[2] = client_fd=7     clients[fd=7]  → type: CLIENT
  pollfds[3] = client_fd=9     clients[fd=9]  → type: CLIENT

If you close fd=7 and remove pollfds[2]:
  - You must ALSO remove clients[fd=7]
  - The pollfds array shifts — indices change
  - But you can avoid shifting with swap-and-pop
```

---

## Concept 3: FD Type Tracking — Why You Need It

### The Problem
When `poll()` says "fd 7 has POLLIN", you need to know: is fd 7 a **listener** (→ call `accept()`) or a **client** (→ call `recv()`) or a **CGI pipe** (→ read CGI output)?

### The Solution
Every FD you track gets a **type tag**:

```cpp
enum FdType {
    FD_LISTEN,     // Listening socket → accept() on POLLIN
    FD_CLIENT,     // Client connection → recv()/send()
    FD_CGI_PIPE    // CGI pipe → read() (Phase 3)
};
```

### The Dispatch Pattern
```cpp
// After poll() returns:
for (each pollfd that has revents != 0) {
    FdType type = lookupType(pollfd.fd);  // Check your map/struct

    if (type == FD_LISTEN && (revents & POLLIN))
        handleAccept(pollfd.fd);           // → accept(), create Client
    else if (type == FD_CLIENT && (revents & POLLIN))
        handleClientRead(pollfd.fd);       // → recv(), feed to parser
    else if (type == FD_CLIENT && (revents & POLLOUT))
        handleClientWrite(pollfd.fd);      // → send() response
    else if (revents & (POLLHUP | POLLERR))
        handleDisconnect(pollfd.fd);       // → close(), cleanup
}
```

### What You Need to Understand
- You're essentially building a **dispatcher** — poll gives you raw events, you route them
- The type determines which handler runs
- This is the same pattern that NGINX, Node.js, and Redis use internally

---

## Concept 4: Client State Machine — Per-Connection State

### The Problem
Each client connection goes through stages:
1. Just accepted → reading request
2. Got full request → processing/building response
3. Have response → writing it out
4. All sent → done (close or keep-alive)

You need to track **where each client is** in this lifecycle.

### The State Machine

```
                  accept()
                     │
                     ▼
            ┌─────────────┐
            │   READING    │ ◄──────────────────────┐
            │              │                         │ keep-alive:
            │ recv() data  │                         │ reset() + loop back
            │ feed() to    │                         │
            │ parser       │                         │
            └──────┬───────┘                         │
                   │ isComplete() or hasError()       │
                   ▼                                  │
            ┌─────────────┐                          │
            │  WRITING     │                          │
            │              │                          │
            │ send() the   │                          │
            │ response     │                          │
            └──────┬───────┘                          │
                   │ all bytes sent                   │
                   ▼                                  │
            ┌─────────────┐          Connection:      │
            │    DONE      │───────── keep-alive? ────┘
            │              │
            │              │───────── close → close(fd), cleanup
            └─────────────┘
```

### What You Need to Understand
- Each client has its **own** state, independent of all others
- The state determines what you do when poll() signals that FD:
  - `STATE_READING` + `POLLIN` → `recv()` + `feed()`
  - `STATE_WRITING` + `POLLOUT` → `send()`
- You change `events` in the pollfd based on state:
  - Reading → watch for `POLLIN`
  - Writing → watch for `POLLOUT`
  - You can watch both if needed (`POLLIN | POLLOUT`)

---

## Concept 5: Partial I/O — The Core Non-Blocking Challenge

### The Problem
In non-blocking mode, `recv()` and `send()` might only process **part** of the data.

### Partial Reads (recv)

```
Client sends 5000 bytes total.

recv() call #1 → returns 1460 bytes (one TCP segment)
recv() call #2 → returns 1460 bytes
recv() call #3 → returns 1460 bytes
recv() call #4 → returns  620 bytes
recv() call #5 → returns   -1 (EAGAIN — no more data right now)
```

You accumulate in a buffer. Person B's `feed()` handles parsing partial HTTP for you.

### Partial Writes (send) — YOU Must Handle This

```cpp
// You want to send a 50KB response
std::string response = buildResponse();  // 50,000 bytes

// First attempt:
ssize_t sent = send(fd, response.c_str(), response.size(), 0);
// sent = 32,000 — kernel buffer only had room for 32K!

// You MUST track:
// - writeBuffer = full response
// - writeOffset = 32000 (how much is already sent)
// - Switch events to POLLOUT
// - On next POLLOUT: send(fd, buf + 32000, 18000, 0)
// - Maybe that also partially sends → keep going until writeOffset == size
```

### What You Need to Understand
- **Never assume** `send()` takes all your data
- Track `writeOffset` per client
- Only watch `POLLOUT` when you have data to write (otherwise `poll()` returns immediately every time — wastes CPU)
- Remove `POLLOUT` once everything is sent

---

## Concept 6: Connection Timeouts — Protecting Against Idle Clients

### The Problem
A client connects but never sends data (or sends partial data and stops). Without timeouts, that FD stays in your poll set **forever**, wasting resources.

### The Solution
```
For each client, track:
    time_t lastActivity;  // Updated on every recv()/send() that transfers data

Every poll() cycle (or every N cycles), scan all clients:
    time_t now = time(NULL);
    if (now - client.lastActivity > TIMEOUT_SECONDS)
        closeClient(fd);  // Remove from poll, close, clean up
```

### What You Need to Understand
- `time(NULL)` returns current time in seconds — cheap to call
- Use `poll()` timeout (e.g., 1000ms) so you wake up periodically even if no I/O events
- Typical timeout: 60 seconds for idle connections
- Update `lastActivity` on **every successful** `recv()` or `send()`
- This prevents resource exhaustion (a slow loris attack opens thousands of idle connections)

---

## Concept 7: POLLOUT Strategy — When to Watch for Write-Readiness

### The Problem
`POLLOUT` is almost **always** ready (the kernel send buffer is rarely full). If you always watch for it, `poll()` returns immediately every cycle → busy loop → 100% CPU.

### The Rule

```
❌ WRONG: Always set events = POLLIN | POLLOUT
   → poll() never sleeps, wastes CPU

✅ RIGHT: 
   - Default: events = POLLIN (only watching for reads)
   - When you have response data to send:
     → Set events = POLLIN | POLLOUT
   - When all response data is sent:
     → Set events = POLLIN (remove POLLOUT)
```

### The Flow
```
1. Client connects     → events = POLLIN
2. Request complete    → build response, set writeBuffer
                       → events = POLLIN | POLLOUT
3. send() some bytes   → partial send, keep POLLOUT
4. send() rest         → all sent!
                       → events = POLLIN (drop POLLOUT)
5. Wait for next request (keep-alive) or close
```

### What You Need to Understand
- POLLOUT is your **"green light to send"** signal
- Only add it when you have data in `writeBuffer`
- Remove it as soon as `writeBuffer` is fully sent
- This is the most common bug in poll-based servers — getting POLLOUT management wrong

---

## Concept 8: Safe pollfd Modification — The Invalidation Trap

### The Problem
You're iterating through the pollfd array processing events. During processing, you accept a new client (add to array) or close a client (remove from array). The array changes **while you're looping over it**.

### The Danger
```cpp
// BROKEN:
for (size_t i = 0; i < pollfds.size(); ++i) {
    if (pollfds[i].revents & POLLIN) {
        if (isListener(pollfds[i].fd)) {
            acceptClient();  // ← pushes to pollfds! size changes!
        }
    }
    if (pollfds[i].revents & POLLHUP) {
        removeClient(i);  // ← erases from pollfds! indices shift!
        // Now pollfds[i] is a DIFFERENT entry — you skip one!
    }
}
```

### The Solution: Defer Modifications
```cpp
// CORRECT:
// 1. Process events, collect changes
std::vector<int> fdsToRemove;
std::vector<int> fdsToAdd;

for (size_t i = 0; i < pollfds.size(); ++i) {
    // ... handle events ...
    // Instead of removing now: fdsToRemove.push_back(fd);
    // Instead of adding now:   fdsToAdd.push_back(newFd);
}

// 2. Apply changes AFTER the loop
for (size_t i = 0; i < fdsToRemove.size(); ++i)
    removePollfd(fdsToRemove[i]);
for (size_t i = 0; i < fdsToAdd.size(); ++i)
    addPollfd(fdsToAdd[i]);
```

### Alternative: Swap-and-Pop Removal
```cpp
// To remove pollfds[i] without shifting:
pollfds[i] = pollfds.back();  // Overwrite with last element
pollfds.pop_back();            // Shrink by 1
// BUT: now you must re-check index i (it has a new entry)
```

### What You Need to Understand
- Never `push_back()` or `erase()` on a vector while iterating it
- Either defer changes or use careful index management
- This is a **common source of crashes** in poll-based servers

---

## Concept 9: POLLHUP vs. recv() == 0 — Two Ways to Detect Disconnect

### The Two Signals

| Signal | When | How you see it |
|--------|------|----------------|
| `recv() == 0` | Client sent FIN (graceful close) | You called recv() and got 0 bytes |
| `POLLHUP` | Other end hung up | poll() sets it in `revents` |
| `POLLERR` | Error on the FD | poll() sets it in `revents` |

### The Order
```
Usually:
1. Client calls close() → sends TCP FIN
2. poll() returns with POLLIN set (there's data to read — the FIN)
3. You call recv() → returns 0
4. Sometimes POLLHUP is also set in revents

Sometimes:
1. Client crashes / network dies
2. poll() returns with POLLHUP or POLLERR
3. You try recv() → returns -1 or 0
```

### What You Need to Understand
- **Always check POLLHUP and POLLERR** in your event loop
- **Always handle recv() == 0** as a disconnect
- If both POLLIN and POLLHUP are set → read first (there might be final data), then close
- Close the FD and clean up in ONE place (avoid double-close bugs)

---

## Concept 10: The accept() Loop — Draining the Queue

### The Problem
When `poll()` signals POLLIN on a listener, there might be **multiple** clients waiting in the accept queue. But `poll()` only tells you "at least one is ready."

### The Solution
```cpp
void handleAccept(int listenFd) {
    while (true) {
        int clientFd = accept(listenFd, ...);
        if (clientFd == -1) {
            // EAGAIN = no more pending connections — we're done
            break;
        }
        fcntl(clientFd, F_SETFL, O_NONBLOCK);
        // Add to poll set + create Client struct
    }
}
```

### What You Need to Understand
- One POLLIN on a listener might mean 1 or 50 pending connections
- Loop `accept()` until EAGAIN to drain the queue
- Each accepted FD needs `fcntl(O_NONBLOCK)` immediately
- If you only accept ONE per poll cycle, you'll fall behind under load

---

## Summary — The 10 Concepts, Ranked by Importance

| # | Concept | Why It's Critical |
|---|---------|-------------------|
| 1 | Event-driven model | The entire architecture — everything else builds on this |
| 2 | Client state machine | Without it, you can't track where each connection is |
| 3 | Partial I/O | The #1 source of bugs — partial recv AND partial send |
| 4 | pollfd array management | Getting add/remove wrong = crashes |
| 5 | FD type tracking | Must know listener vs client vs CGI pipe |
| 6 | POLLOUT strategy | Getting this wrong = 100% CPU or stuck responses |
| 7 | Safe modification during iteration | Subtle bug that only shows under load |
| 8 | Connection timeouts | Required by subject, prevents resource exhaustion |
| 9 | Disconnect detection | POLLHUP + recv()==0 — must handle both |
| 10 | Accept loop draining | Performance under concurrent connections |

---

## What You Already Know (From Phase 1)

✅ Individual syscalls: socket, bind, listen, accept, recv, send, close, fcntl, poll
✅ Non-blocking I/O concept (EAGAIN)
✅ Byte order (htons/htonl)
✅ SIGPIPE/SIGINT handling
✅ FD lifecycle and RAII

## What's NEW in Phase 2

🆕 Combining poll + accept + recv + send into **one unified loop**
🆕 Per-client state tracking (the Client struct)
🆕 Dynamic pollfd array management (add/remove FDs safely)
🆕 Partial write handling with writeOffset
🆕 POLLOUT toggling (on when data ready, off when sent)
🆕 Timeout scanning
🆕 Event dispatching by FD type
