# The Webserv Subject — Explained for Person A

> Every subject requirement that touches your work, what it really means, why they want it, and where the pain is.

---

## 1. "Your program must not crash under any circumstances"

### What they want
Zero segfaults, zero aborts, zero unexpected exits. Even if `malloc` fails, even if a client sends garbage, even if 1000 clients connect at once. Your server must keep running.

### Why they want it
A real web server (NGINX, Apache) runs for **months** without restarting. If your server crashes from one bad client, every other connected client loses their connection. That's unacceptable in production. They want you to think like an infrastructure engineer — your code is the foundation that must never fall.

### The challenge for you
Every system call can fail: `socket()`, `bind()`, `accept()`, `recv()`, `send()`, `poll()`. You must check **every single return value** and handle the failure gracefully (log it, clean up, continue) instead of crashing.

```
The trap: You test with 1 client, it works. You test with 100 clients,
accept() fails because you hit the FD limit. If you don't handle that
failure → crash → grade 0.
```

---

## 2. "Your server must remain non-blocking at all times"

### What they want
No function call should ever freeze your server waiting for data. Every socket (listening AND client) must be in non-blocking mode (`O_NONBLOCK`).

### Why they want it
Imagine a server with 100 connected clients. Client #7 is on a slow connection. If your `recv(client_7)` **blocks** (freezes waiting for data), then clients #1-6 and #8-100 are ALL frozen too — because you're stuck in one `recv()` call. The entire server becomes unresponsive because of ONE slow client.

Non-blocking solves this: `recv()` checks if data is ready. If yes → read it. If no → returns immediately with `EAGAIN`, and you move on to the next client.

### The challenge for you
Non-blocking changes **everything** about how you think:
- `accept()` can return "no one is waiting" → not an error, just try again later
- `recv()` can return "no data yet" → not an error, just wait for next poll cycle
- `send()` can say "I only sent 50 of your 5000 bytes" → you must save the remaining 4950 and send them later
- You must **buffer** partial data and track where you left off for every client

```
The mental shift: In blocking code, one function call = one complete action.
In non-blocking code, one action = many partial calls spread across many
loop iterations. This is the hardest concept in the project.
```

---

## 3. "Use only 1 poll() for ALL I/O operations (listen included)"

### What they want
Your entire server has **one single event loop** with **one single `poll()` call** (or `epoll`/`select`). That one call monitors:
- All listening sockets (for new connections)
- All client sockets (for incoming data AND outgoing responses)
- CGI pipes (for CGI output) — when you integrate with Person C

Everything goes through this ONE call. No second `poll()` hidden somewhere else. No separate loop for listening vs reading.

### Why they want it
This is the **Reactor Pattern** — the same architecture used by NGINX, Node.js, and Redis. It's the industry-standard way to build high-performance servers. They want you to learn it because:

1. **It's efficient** — one thread handles thousands of connections
2. **It's the alternative to multi-threading** — no threads, no locks, no race conditions
3. **It's how real infrastructure works** — if you ever work on backend systems, you'll see this pattern everywhere

### The challenge for you
You need ONE data structure (a `std::vector<struct pollfd>` or similar) that holds **every FD in your entire program**. When a new client connects, you add their FD. When they disconnect, you remove it. When CGI starts, you add the pipe FD. This dynamic list must be managed carefully — adding/removing while iterating is a classic source of bugs.

```
The constraint: You cannot have a separate loop that just polls listening
sockets, and another that polls clients. It's ONE loop, ONE poll(), ALL FDs.
```

---

## 4. "Never do a read or write without going through poll()"

### What they want
Before calling `recv()` on a client socket, `poll()` must have told you that socket has data ready (`POLLIN`). Before calling `send()`, `poll()` must have told you that socket is ready to write (`POLLOUT`). No exceptions.

### Why they want it
Without this rule, you'd be tempted to call `recv()` "just to check" — which works on non-blocking sockets (returns `EAGAIN`), but it's wasteful. More importantly, on some systems and edge cases, calling `recv()` on a non-ready socket can behave unexpectedly.

The real lesson: **event-driven programming**. You don't ask "is there data?" — you wait to be **told** there's data. This is the core philosophy of `poll()`/`epoll()`. Your code should react to events, not poll in a busy loop.

### The challenge for you
You must structure your code so that **every read/write is triggered by a poll event**. Your event loop becomes the single decision-maker:

```
poll() says POLLIN on fd 7  → THEN you call recv(7)
poll() says POLLOUT on fd 7 → THEN you call send(7)
poll() says nothing about 7 → you don't touch fd 7 at all
```

---

## 5. "Checking errno after read/write is strictly forbidden"

### What they want
After `recv()` or `send()`, you cannot look at `errno` to decide what to do. You must rely only on the **return value** of the function.

### Why they want it
This is a subtle but important systems programming lesson. `errno` is a **global variable** — if anything else happens between your `recv()` and your `errno` check (a signal handler, for example), `errno` might be overwritten. In a non-blocking, single-threaded server, this is unlikely but possible.

More practically: if your code works correctly with `poll()` + non-blocking, you **shouldn't need `errno`** to make decisions after read/write. The return value tells you everything:
- `recv()` returns `> 0` → got data (this many bytes)
- `recv()` returns `0` → client disconnected
- `recv()` returns `-1` → error (but since you only call recv after POLLIN, this shouldn't happen in normal flow)

### The challenge for you
You're probably used to checking `errno == EAGAIN` after non-blocking calls. You can't do that here. Instead, trust `poll()`: if `poll()` said the FD is ready, `recv()` should succeed. If it doesn't, something is wrong — close the connection.

```
The exception: You CAN check errno in contexts that aren't read/write.
For example, after socket(), bind(), listen() — those are setup calls, not
I/O operations. The rule applies specifically to recv/send/read/write.
```

---

## 6. "Your server must be able to listen to multiple ports"

### What they want
One server process, multiple listening sockets. Config says `listen 8080` and `listen 8081` → your server creates two listening sockets and watches both in the same `poll()` call.

### Why they want it
Real web servers do this all the time — port 80 for HTTP, port 443 for HTTPS, or different ports for different websites on the same machine. They want you to understand that a "server" isn't tied to one port. It's a program that can manage many listeners.

### The challenge for you
When `poll()` reports `POLLIN` on a listening socket, you need to know **which** listening socket it is (which port, which server block config). You need a mapping from FD → server configuration so that new clients inherit the right settings.

```
The design question: How do you distinguish a listening FD from a client FD
in your poll loop? Common approach: keep a set/map of listening FDs and
check membership. If it's a listener → accept(). If not → recv()/send().
```

---

## 7. "A request to your server should never hang indefinitely"

### What they want
If a client connects but never sends data, your server must eventually close that connection. No connection should live forever doing nothing.

### Why they want it
This is defense against the **Slowloris attack** — a real-world attack where a malicious client opens connections and deliberately sends data very slowly, tying up server resources. Without timeouts, one attacker can exhaust all your server's connection slots with idle connections.

### The challenge for you
You need to track **when each client last did something** (last `recv()` or `send()` that transferred data). In every loop iteration, check if any client has been idle longer than your timeout threshold. If so, close them.

```
Implementation thought: You can use time() or gettimeofday() to record
timestamps. Check them either every loop iteration, or use poll()'s
timeout parameter to wake up periodically for cleanup.
```

---

## 8. "Handle client disconnections gracefully"

### What they want
When a client closes their browser or their connection drops, your server must:
1. Detect it (via `recv()` returning 0, or `POLLHUP`)
2. Close the socket FD
3. Remove the FD from the poll set
4. Free any associated memory (buffers, state)
5. Continue serving other clients — no crash, no leak

### Why they want it
In a real server, clients disconnect constantly — browser closed, network dropped, timeout. If each disconnect leaks one FD, after 1000 disconnects your server is dead. They want you to build a server that can run **indefinitely** with clients constantly connecting and disconnecting.

### The challenge for you
The tricky part: removing an FD from your `pollfd` array **while you're iterating over it**. If you remove element 5 from a vector while in a for-loop at index 5, you either skip an element or access invalid memory. You need a clean strategy for deferred removal.

---

## 9. "Stress test your server to ensure availability"

### What they want
Hit your server with hundreds of simultaneous connections. It must not crash, not leak FDs, not slow to a crawl, and not refuse valid connections.

### Why they want it
Any server can handle 1 client. The real test is **concurrent load**. This forces you to find:
- FD leaks (server dies after N connections)
- Memory leaks (server slows down over time)
- Buffer bugs (data from client A showing up in client B's response)
- Timeout bugs (clients not being cleaned up)
- Poll set management bugs (stale FDs causing errors)

### The challenge for you
You'll use tools like `siege` or `ab` (Apache Bench):
```bash
siege -c 100 -t 30s http://localhost:8080/
ab -n 10000 -c 100 http://localhost:8080/
```
The first time you run these, things WILL break. That's the point. Stress testing is where you find bugs that manual testing never reveals.

---

## Summary: Person A's World in One Picture

```
                    ┌──────────────────────────────────────┐
                    │         WHAT THE SUBJECT WANTS        │
                    └──────────────────┬───────────────────┘
                                       │
              ┌────────────────────────┼────────────────────────┐
              │                        │                        │
    ┌─────────▼─────────┐   ┌─────────▼─────────┐   ┌─────────▼─────────┐
    │   NEVER CRASH      │   │  NEVER BLOCK       │   │  NEVER LEAK       │
    │                     │   │                     │   │                     │
    │ • Check all returns │   │ • O_NONBLOCK all   │   │ • close() every FD │
    │ • Handle all errors │   │ • Handle EAGAIN     │   │ • Remove from poll │
    │ • No segfaults      │   │ • Partial read/write│   │ • Free buffers     │
    │ • Survive bad input │   │ • Buffer everything │   │ • Survive siege    │
    └─────────────────────┘   └─────────────────────┘   └─────────────────────┘
              │                        │                        │
              └────────────────────────┼────────────────────────┘
                                       │
                              ┌────────▼────────┐
                              │  ONE POLL LOOP   │
                              │  RULES THEM ALL  │
                              └─────────────────┘
```

> [!TIP]
> **The subject is essentially saying:** Build something that runs forever, handles anything thrown at it, and never freezes or leaks. That's what makes a real server — and that's what makes this project hard.
