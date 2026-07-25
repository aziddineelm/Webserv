# I/O Multiplexing — Deep Dive

> You know the syscall APIs. This goes deeper: **why** multiplexing exists, **how** the kernel implements it, and the mental models that make poll()-based programming intuitive.

---

## Chapter 1: The Problem That Created I/O Multiplexing

### 1.1 The Scenario

You have a web server. 500 clients are connected. At any given moment:
- 3 clients are sending data right now
- 2 clients have full responses ready to receive
- 495 clients are idle (thinking, typing, loading JavaScript)

**The question:** How do you figure out which 5 FDs need attention, without wasting time on the other 495?

### 1.2 The Four Approaches (Historical Evolution)

```
Approach 1: ONE PROCESS PER CLIENT (Apache 1.x, ~1995)
─────────────────────────────────────────────────────
fork() for each connection. Each child calls blocking recv().

  Parent:  accept() → fork() → accept() → fork() → ...
  Child 1: recv(client_1) ████████ blocked, waiting...
  Child 2: recv(client_2) ████████ blocked, waiting...
  Child 3: recv(client_3) ████████ blocked, waiting...

  ✅ Simple code
  ❌ 500 clients = 500 processes = huge RAM (~500MB)
  ❌ Context switching overhead (OS switches between 500 processes)
  ❌ fork() is slow
  ❌ 42 forbids fork() except for CGI


Approach 2: ONE THREAD PER CLIENT (Apache 2.x worker, ~2000)
─────────────────────────────────────────────────────
pthread_create() for each connection. Each thread calls blocking recv().

  Thread 1: recv(client_1) ████████ blocked
  Thread 2: recv(client_2) ████████ blocked
  Thread 3: recv(client_3) ████████ blocked

  ✅ Lighter than processes (~2MB stack each)
  ❌ 500 clients = 500 threads = still ~1GB RAM
  ❌ Thread synchronization bugs (mutexes, deadlocks)
  ❌ C++98 has no standard threads
  ❌ 42 doesn't allow threading libraries


Approach 3: BUSY POLLING / SPIN LOOP (naive non-blocking)
─────────────────────────────────────────────────────
Set all sockets non-blocking, loop through them checking each one.

  while (true) {
      for each client_fd:
          n = recv(client_fd, buf, ...) // non-blocking
          if (n > 0) handle_data()
          // if EAGAIN → skip, try next
  }

  ✅ Single thread, handles all clients
  ❌ Burns 100% CPU constantly (checking 495 idle FDs every loop)
  ❌ This is what your Phase 1 code does with usleep(100ms)
  ❌ Unacceptable for production


Approach 4: I/O MULTIPLEXING (NGINX, Node.js, Redis, YOUR SERVER)
─────────────────────────────────────────────────────
Ask the KERNEL to watch all FDs and wake you ONLY when something happens.

  while (true) {
      ready = poll(all_fds)  // process SLEEPS until an FD is ready
      // CPU usage: ~0% while waiting
      for each ready_fd:
          handle_event(ready_fd)  // only process the 5 active FDs
  }

  ✅ Single thread
  ✅ Near-zero CPU when idle
  ✅ Handles thousands of connections
  ✅ No threading bugs
  ✅ Exactly what 42 requires
```

### 1.3 Why Multiplexing Wins

| Metric | Process-per-client | Thread-per-client | Busy poll | **I/O Multiplexing** |
|--------|-------------------|-------------------|-----------|---------------------|
| Memory (500 clients) | ~500MB | ~1GB | ~1MB | **~1MB** |
| CPU when idle | ~0% (sleeping) | ~0% (sleeping) | 100% | **~0%** |
| Max connections | ~1000 | ~10,000 | unlimited | **unlimited** |
| Code complexity | Low | Medium (sync) | Low | **Medium (state)** |
| 42 allowed? | ❌ (fork) | ❌ (threads) | ❌ (wastes CPU) | **✅** |

The tradeoff: multiplexing requires you to manage **per-connection state manually** (your Client struct + state machine). With threads, each thread has its own stack as implicit state. With multiplexing, you trade automatic state for efficiency.

---

## Chapter 2: How the Kernel Implements It

### 2.1 The Kernel's Perspective

Every socket has internal structures in the kernel:

```
Kernel memory for fd=7 (a client socket):
┌─────────────────────────────────────────┐
│ Socket Object                           │
│                                         │
│ ┌─────────────────┐  ┌───────────────┐  │
│ │  Receive Buffer  │  │  Send Buffer  │  │
│ │  [H][E][L][L][O] │  │  [empty]      │  │
│ │  5 bytes waiting │  │  space: 128KB │  │
│ └─────────────────┘  └───────────────┘  │
│                                         │
│ State: ESTABLISHED                      │
│ Wait Queue: [pointer to your process]   │  ← KEY: who to wake up
│                                         │
└─────────────────────────────────────────┘
```

The **wait queue** is the magic. When you call `poll()`, the kernel registers your process on the wait queue of EVERY FD you're watching. Then your process sleeps.

### 2.2 What Happens Inside poll() — Step by Step

```
YOUR CODE                           KERNEL
─────────                           ──────

poll(fds, 3, 5000)
  │
  ├─1. Kernel scans fds[0..2]
  │    For each FD:
  │      - Check: does this FD have data/space/events RIGHT NOW?
  │      - Register your process on this FD's wait queue
  │
  ├─2. Any FD ready right now?
  │    ├─ YES → Fill revents, return immediately
  │    └─ NO  → Put your process to SLEEP
  │              (process state: RUNNING → SLEEPING)
  │              (CPU is FREE to do other work)
  │
  │    ... time passes ... your process uses ZERO CPU ...
  │
  ├─3. A TCP packet arrives for fd=7
  │    Network card → interrupt → kernel network stack
  │    Kernel puts data in fd=7's receive buffer
  │    Kernel checks fd=7's wait queue → YOUR PROCESS is there
  │    Kernel WAKES your process
  │    (process state: SLEEPING → RUNNING)
  │
  └─4. Kernel fills revents for the ready FDs
       Returns number of ready FDs

Your code continues, processes the ready FDs
```

### 2.3 The Wait Queue Mechanism

```
BEFORE poll():
  fd=3 (listener):  wait_queue = []
  fd=7 (client A):  wait_queue = []
  fd=9 (client B):  wait_queue = []
  Your process: RUNNING

DURING poll() — kernel registers you:
  fd=3 (listener):  wait_queue = [→ your_process]
  fd=7 (client A):  wait_queue = [→ your_process]
  fd=9 (client B):  wait_queue = [→ your_process]
  Your process: SLEEPING

EVENT: data arrives on fd=7:
  Kernel: "fd=7 has data → check wait_queue → wake your_process"
  Your process: RUNNING again

AFTER poll() returns — kernel unregisters you:
  fd=3 (listener):  wait_queue = []
  fd=7 (client A):  wait_queue = []
  fd=9 (client B):  wait_queue = []
```

This is why `poll()` is efficient — your process truly **sleeps** and consumes **zero CPU** until an event occurs. The hardware interrupt from the network card triggers the chain of wakeups.

---

## Chapter 3: select() vs poll() vs epoll() — The Real Differences

### 3.1 select() — The Original (1983)

```c
fd_set readfds;
FD_ZERO(&readfds);
FD_SET(3, &readfds);  // watch fd 3
FD_SET(7, &readfds);  // watch fd 7

select(max_fd + 1, &readfds, &writefds, &exceptfds, &timeout);

if (FD_ISSET(7, &readfds))  // check if fd 7 is ready
    recv(7, ...);
```

**How it works internally:**
```
1. Kernel receives a BITMASK of FDs (1024 bits max)
   Bit 3 = 1, Bit 7 = 1, all others = 0

2. Kernel scans from bit 0 to bit max_fd
   For each set bit: check if that FD is ready

3. Kernel MODIFIES the bitmask in-place
   Only ready FDs remain set

4. You scan the bitmask to find which FDs are ready
```

**Problems:**
- **Hard limit: 1024 FDs** (FD_SETSIZE) — can't serve more than ~1020 clients
- **Kernel scans ALL bits** from 0 to max_fd every call — O(max_fd)
- **Destroys the fd_set** — you must rebuild it every iteration
- **Three separate sets** for read/write/error — cumbersome API

### 3.2 poll() — The Fix (1986)

```c
struct pollfd fds[2];
fds[0] = {.fd = 3, .events = POLLIN, .revents = 0};
fds[1] = {.fd = 7, .events = POLLIN, .revents = 0};

poll(fds, 2, timeout_ms);

if (fds[1].revents & POLLIN)
    recv(7, ...);
```

**How it works internally:**
```
1. Kernel receives an ARRAY of pollfd structs

2. For EACH struct in the array:
   - Check if that FD has matching events
   - Write result to revents field

3. Return count of FDs with non-zero revents
```

**Improvements over select:**
- **No FD limit** — array can be any size
- **Doesn't destroy input** — events stays unchanged, revents is separate
- **Cleaner API** — one struct per FD instead of three bitmasks

**Remaining problem:**
- **O(n) every call** — kernel must scan the ENTIRE array, even if only 3 of 10,000 are active
- **O(n) in your code** — you must loop through ALL entries to find which have revents

```
poll() with 10,000 FDs, 3 active:
  Kernel work: check all 10,000 → find 3 ready     ← O(n) waste
  Your work:   loop all 10,000 → process 3 events   ← O(n) waste
```

### 3.3 epoll() — The Linux Solution (2002)

```c
// Setup (once):
int epfd = epoll_create(1);
struct epoll_event ev = {.events = EPOLLIN, .data.fd = 7};
epoll_ctl(epfd, EPOLL_CTL_ADD, 7, &ev);   // register fd 7

// Event loop:
struct epoll_event ready[64];
int n = epoll_wait(epfd, ready, 64, timeout_ms);
for (int i = 0; i < n; i++)               // loop ONLY ready FDs
    handle(ready[i].data.fd);
```

**How it works internally:**
```
1. epoll_create() → kernel creates a persistent interest list
   (a red-black tree of FDs you're watching)

2. epoll_ctl(ADD) → kernel adds fd=7 to the interest list
   AND installs a callback on fd=7's wait queue

3. When data arrives on fd=7:
   - Network interrupt fires
   - Kernel puts data in fd=7's receive buffer
   - The callback MOVES fd=7 to epoll's READY LIST

4. epoll_wait() → kernel just returns the ready list
   No scanning needed — the ready list is pre-built by callbacks
```

**The key insight:**
```
poll():   You give the kernel ALL FDs every time
          Kernel scans ALL of them → O(n)

epoll():  You register FDs ONCE (epoll_ctl)
          Kernel maintains a ready list via callbacks
          epoll_wait() just drains the ready list → O(ready)
```

**Performance comparison (10,000 connections, 3 active):**

| | poll() | epoll() |
|---|---|---|
| Per-call kernel work | Scan 10,000 FDs | Return 3 ready FDs |
| Per-call your work | Loop 10,000 entries | Loop 3 entries |
| Complexity | O(total FDs) | O(active FDs) |
| Copying | Copy 10,000 pollfds to kernel each call | Nothing (interest list is persistent) |

### 3.4 Which Should YOU Use?

For Webserv (42 project):

| Factor | poll() | epoll() |
|--------|--------|---------|
| Portability | ✅ Works everywhere | ❌ Linux only |
| Complexity | Simpler | More setup code |
| Performance | Fine for <1000 clients | Needed for >10,000 |
| 42 eval machines | ✅ Always works | ✅ Always Linux |
| Evaluation | Evaluators expect poll() | Bonus impression |

**Recommendation:** Use `poll()`. It's simpler, portable, and more than enough for 42's stress tests. You can always swap to epoll later — the event loop logic is identical.

---

## Chapter 4: Level-Triggered vs Edge-Triggered

This is the most misunderstood concept in I/O multiplexing.

### 4.1 The Analogy

```
You have a mailbox. You want to know when there's mail.

LEVEL-TRIGGERED (default for poll and epoll):
  "Is there mail in the box RIGHT NOW?"
  → YES every time you check, as long as mail is sitting there
  → You can check multiple times, it keeps saying YES
  → You can read ONE letter, come back later for the rest

EDGE-TRIGGERED (epoll with EPOLLET):
  "Did NEW mail arrive SINCE LAST TIME I checked?"
  → YES only ONCE, when mail first arrives
  → If you don't read ALL of it, you won't be notified again
  → You MUST read everything in one go
```

### 4.2 Concrete Example

Client sends 5000 bytes. Your buffer is 1024 bytes.

```
LEVEL-TRIGGERED:
  epoll_wait() → fd=7 ready (POLLIN)     ← "there IS data"
  recv(7, buf, 1024) → 1024 bytes        read 1024, 3976 remaining
  
  epoll_wait() → fd=7 ready (POLLIN)     ← "there STILL IS data"
  recv(7, buf, 1024) → 1024 bytes        read 1024, 2952 remaining
  
  epoll_wait() → fd=7 ready (POLLIN)     ← "there STILL IS data"
  recv(7, buf, 1024) → 1024 bytes        read 1024, 1928 remaining
  
  ... keeps reporting ready until buffer is empty ...


EDGE-TRIGGERED:
  epoll_wait() → fd=7 ready (POLLIN)     ← "NEW data arrived"
  recv(7, buf, 1024) → 1024 bytes        read 1024, 3976 remaining
  
  epoll_wait() → ??? NOTHING             ← no NEW data arrived
  
  3976 bytes are STUCK in the kernel buffer forever!
  (until the client sends more data, triggering a new edge)
```

### 4.3 Edge-Triggered Fix

With edge-triggered, you MUST drain the entire buffer in one go:

```c
// Edge-triggered pattern: read until EAGAIN
while (true) {
    n = recv(fd, buf, sizeof(buf), 0);
    if (n > 0)
        process(buf, n);
    else if (n == -1 && errno == EAGAIN)
        break;  // buffer fully drained
    else if (n == 0)
        disconnect();
}
```

### 4.4 What poll() Uses

**poll() is always level-triggered.** There is no edge-triggered option.

This means:
- If fd=7 has data and you don't read it, poll() will report it again next call ✅
- You can read partially and come back later ✅
- Safer and simpler — harder to lose data ✅
- Slight performance cost — poll keeps reporting FDs you haven't fully handled

**For your Webserv: level-triggered with poll() is perfect. Don't worry about edge-triggered.**

---

## Chapter 5: The poll() Event Flags — Complete Reference

### 5.1 Input Flags (you set in `events`)

| Flag | Value | Meaning |
|------|-------|---------|
| `POLLIN` | 0x001 | There is data to read (or a new connection on a listener) |
| `POLLOUT` | 0x004 | Writing won't block (kernel send buffer has space) |
| `POLLPRI` | 0x002 | Urgent/out-of-band data (rarely used, not needed for HTTP) |

### 5.2 Output-Only Flags (kernel sets in `revents`, you never set these)

| Flag | Value | Meaning |
|------|-------|---------|
| `POLLERR` | 0x008 | Error on the FD. Call `getsockopt(fd, SOL_SOCKET, SO_ERROR, ...)` for details |
| `POLLHUP` | 0x010 | Hang up — the other end closed the connection |
| `POLLNVAL` | 0x020 | Invalid FD — you closed it but forgot to remove from the array (YOUR BUG) |

### 5.3 How revents Combines Flags

`revents` is a **bitmask** — multiple flags can be set at once:

```
revents = POLLIN | POLLHUP  (value: 0x011)
  → "There's final data to read AND the client disconnected"
  → Read first, then close

revents = POLLIN | POLLOUT  (value: 0x005)
  → "You can both read and write on this FD"
  → Handle read first (get request), then write (send response)

revents = POLLHUP | POLLERR (value: 0x018)
  → "Connection is broken"
  → Close and clean up immediately
```

### 5.4 Checking Flags with Bitwise AND

```cpp
// CORRECT — bitwise AND to check individual flags:
if (fds[i].revents & POLLIN)   // is the POLLIN bit set?
if (fds[i].revents & POLLOUT)  // is the POLLOUT bit set?
if (fds[i].revents & POLLHUP)  // is the POLLHUP bit set?

// WRONG — equality check fails when multiple flags are set:
if (fds[i].revents == POLLIN)  // ❌ fails if revents = POLLIN|POLLHUP
```

### 5.5 Processing Order

When multiple flags are set on the same FD, process in this order:

```
1. Check POLLERR/POLLNVAL first → error, close immediately
2. Check POLLIN → read data (there might be final data before HUP)
3. Check POLLHUP → client disconnected (after reading any remaining data)
4. Check POLLOUT → send response data
```

Why read before checking HUP? Because the kernel can set both `POLLIN | POLLHUP` — meaning "the client sent final data AND closed." You want that data.

---

## Chapter 6: The poll() Timeout — Connection to Your Event Loop

### 6.1 Three Timeout Modes

```cpp
// Mode 1: Block forever (bad for a server)
poll(fds, nfds, -1);
// Process sleeps until ANY event. But you never check timeouts!

// Mode 2: Return immediately (busy loop — wastes CPU)
poll(fds, nfds, 0);
// Always returns immediately. 100% CPU usage. Terrible.

// Mode 3: Timed wait (correct for your server)
poll(fds, nfds, 1000);  // Wait up to 1 second
// Sleeps up to 1000ms. Returns early if an event occurs.
// Every 1 second (worst case), you wake up and can check timeouts.
```

### 6.2 Why You Need a Timeout

```
Without timeout (poll with -1):
  Client connects at 10:00:00
  Client sends nothing
  No other events happen
  poll() sleeps FOREVER
  At 10:05:00, the idle client is still connected, wasting a FD
  You never get a chance to check and clean it up

With timeout (poll with 1000ms):
  Client connects at 10:00:00
  Client sends nothing
  10:00:01 → poll() wakes up (timeout). You check: "any idle clients?" Not yet.
  10:00:02 → poll() wakes up (timeout). Check again. Not yet.
  ...
  10:01:00 → poll() wakes up. Check: "client idle for 60 seconds!" → close it.
```

### 6.3 Your Event Loop with Timeout

```cpp
#define POLL_TIMEOUT_MS  1000   // Wake up at least every second
#define CLIENT_TIMEOUT   60     // Close clients idle for 60 seconds

while (g_running) {
    int ready = poll(&_pollfds[0], _pollfds.size(), POLL_TIMEOUT_MS);

    if (ready < 0) {
        if (errno == EINTR) continue;  // Signal interrupted poll → retry
        break;  // Real error
    }

    if (ready > 0) {
        // Process events on ready FDs
        handleEvents();
    }

    // Always runs (even if ready == 0, meaning timeout):
    checkTimeouts();
}
```

---

## Chapter 7: poll() vs Your Phase 1 Code — The Transformation

### 7.1 Phase 1 (What You Have Now)

```cpp
void Server::run() {
    while (_running && g_running) {
        for (size_t i = 0; i < _sockets.size(); ++i) {
            _acceptConnection(*_sockets[i]);     // try accept on each listener
        }
        usleep(100000);  // 100ms sleep to prevent busy-spinning
    }
}
```

**Problems:**
1. `usleep(100ms)` → up to 100ms latency before accepting a connection
2. No client tracking → accepted clients are immediately closed
3. No recv/send → can't actually serve HTTP
4. Only checks listeners → no client I/O

### 7.2 Phase 2 (What You're Building)

```cpp
void Server::run() {
    while (_running && g_running) {
        int ready = poll(&_pollfds[0], _pollfds.size(), 1000);

        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (size_t i = 0; i < _pollfds.size() && ready > 0; ++i) {
            if (_pollfds[i].revents == 0)
                continue;  // This FD has no events, skip
            ready--;       // One less to find

            int fd = _pollfds[i].fd;

            if (isListenFd(fd)) {
                handleAccept(fd);          // POLLIN on listener → accept
            } else {
                if (_pollfds[i].revents & (POLLERR | POLLNVAL))
                    closeClient(fd);
                else if (_pollfds[i].revents & POLLIN)
                    handleRead(fd);        // Client sent data → recv + parse
                else if (_pollfds[i].revents & POLLOUT)
                    handleWrite(fd);       // Ready to send → send response
                if (_pollfds[i].revents & POLLHUP)
                    closeClient(fd);
            }
        }

        checkTimeouts();
    }
}
```

**What changed:**
- `usleep()` → `poll()` — zero latency, near-zero CPU
- Accept-and-close → accept, track, read, parse, respond, then close
- Listeners only → listeners AND clients in one unified poll set
- No state → per-client state machine (Client struct)

---

## Chapter 8: The C10K Problem — Why This All Matters

### 8.1 The History

In 1999, Dan Kegel asked: "How do you handle 10,000 simultaneous connections on one server?" This was called the **C10K problem**.

```
1999: Most servers used thread-per-connection
      10,000 threads × 2MB stack = 20GB RAM
      Context switching between 10,000 threads → CPU meltdown
      Answer: you can't (with threads)

2002: Linux added epoll
      10,000 connections × 1 poll entry = ~80KB
      One thread, one epoll instance
      Answer: easy

Today: NGINX, Node.js, Redis all use this model
       Handle 100,000+ connections on a single thread
       The C10K problem is solved — now it's C10M (10 million)
```

### 8.2 Why Your Webserv Uses This Model

The 42 subject says:
> "It must be non-blocking and use only 1 poll() for all the I/O operations"

You're building a mini-NGINX. Same architecture, same model, same reasons. Your server won't handle 10K connections in practice, but the architecture is production-grade.

---

## Chapter 9: Common Pitfalls — What Goes Wrong

### 9.1 Forgetting to Remove Closed FDs from poll

```
You close(fd=7) but forget to remove pollfds entry for fd=7.
Next poll() call → POLLNVAL on fd=7 ("invalid FD")
→ Might cause infinite loop of POLLNVAL events
```

### 9.2 Always Watching POLLOUT

```
Set events = POLLIN | POLLOUT for a client with no response to send.
POLLOUT is almost always ready (kernel buffer is rarely full).
→ poll() returns immediately every time → busy loop → 100% CPU
```

### 9.3 Not Handling EINTR

```cpp
int ready = poll(fds, n, 1000);
if (ready < 0) {
    // DON'T just break!
    // A signal (SIGINT) can interrupt poll(), setting errno to EINTR.
    // This is normal — just retry.
    if (errno == EINTR)
        continue;
    // Only break on REAL errors
    break;
}
```

### 9.4 Modifying pollfds Vector During Iteration

```
Iterating pollfds[0..9]. At i=3, you accept a new client and push_back().
Vector might reallocate → ALL pointers/indices invalidated → crash.
Solution: defer additions/removals until after the loop.
```

### 9.5 Not Draining accept() Queue

```
5 clients connect simultaneously. poll() returns POLLIN on listener ONCE.
You call accept() once → get 1 client.
4 clients are stuck in the queue until next poll() cycle.
Solution: loop accept() until EAGAIN.
```

---

## Summary — What You Now Understand

| Concept | Your Understanding |
|---------|-------------------|
| Why multiplexing exists | Thread-per-client doesn't scale; busy-polling wastes CPU |
| How poll() works internally | Kernel registers you on FD wait queues, wakes you on events |
| select vs poll vs epoll | select=limited, poll=O(n) per call, epoll=O(ready) via callbacks |
| Level vs Edge triggered | poll()=level (safer, reports until you handle it) |
| Event flags | POLLIN/POLLOUT/POLLHUP/POLLERR and how to check with bitwise AND |
| Timeout strategy | 1000ms to balance responsiveness with timeout checking |
| The transformation | Your Phase 1 usleep loop → Phase 2 poll-driven event loop |
| C10K context | You're building the same architecture as NGINX/Redis |
| Common pitfalls | POLLNVAL, POLLOUT busy-loop, EINTR, vector invalidation |
