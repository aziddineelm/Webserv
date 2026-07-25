# Person A — What to Know Before Phase 1 (Teammate Impact)

> These are decisions you make in Phase 1 that **lock in** how Person B and C will write their code. Get these wrong → painful refactoring for the whole team later.

---

## 1. The Client Struct — Everyone Depends on This

You will create a struct or class to track each connected client. **Person B and Person C will both need to read/write fields in this struct.** Agree on its shape before anyone codes.

```cpp
// Person A owns this, but B and C use it
struct Client {
    int             fd;              // A owns: the socket FD
    std::string     readBuffer;      // A fills: raw bytes from recv()
    std::string     writeBuffer;     // B fills: serialized HTTP response
    int             state;           // A manages: READING, PROCESSING, WRITING, CLOSING
    time_t          lastActivity;    // A manages: for timeout
    // Person C will need:
    int             cgiFd;           // C needs: pipe FD for CGI output
    pid_t           cgiPid;          // C needs: child process ID
};
```

### What to agree on with B:
- **Who appends to `readBuffer`?** → You (A) do, via `recv()`
- **Who reads from `readBuffer`?** → Person B parses it
- **Who fills `writeBuffer`?** → Person B puts the full HTTP response string here
- **Who drains `writeBuffer`?** → You (A) do, via `send()`
- **When does B get called?** → When you detect a complete request in `readBuffer`

### What to agree on with C:
- **Who adds CGI pipe FDs to the poll set?** → You (A) or C? Agree now.
- **Who stores `cgiFd` and `cgiPid`?** → In the Client struct? In a separate map?

---

## 2. How Person B Receives Data From You

This is the **#1 integration point**. You `recv()` raw bytes. Person B needs to parse them into an HTTP Request. Agree on the handoff:

### Option A: You pass a reference to the buffer
```cpp
// You call this when readBuffer has data
bool Request::parse(std::string &buffer);
// B reads from buffer, returns true when a full request is found
// B removes parsed bytes from buffer (so leftover stays for next request)
```

### Option B: You pass the raw string
```cpp
// You call this with whatever recv() gave you
Request::feed(const std::string &chunk);
// B accumulates internally, you ask B: "is a request ready?"
bool Request::isComplete();
```

### Why this matters now
If you don't agree, you might build your event loop assuming Option A, and Person B builds their parser assuming Option B. Then at integration (week 3-4) → everything breaks and you both refactor.

**Recommendation:** Go with Option A — it's simpler. You own the buffer, B parses from it.

---

## 3. How Person B Gives You the Response

After B processes a request, they produce an HTTP response. How does it get back to you?

### Agree on this:
```cpp
// B produces a fully serialized response string:
std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello";

// You store it:
client.writeBuffer = response;

// You register POLLOUT for this client
// Your event loop calls send() when ready
```

### Key question: Who triggers POLLOUT?
- After B fills `writeBuffer`, does B call something like `server.markReadyToWrite(fd)`?
- Or does your event loop always check `if (!client.writeBuffer.empty()) → register POLLOUT`?

**Recommendation:** Your event loop checks. Simpler. B just fills the buffer and returns.

---

## 4. How Person C's Config Reaches You

Person C parses the config file into a `ServerConfig` object. You need data from it **at startup** to know which ports to listen on.

### What you need from C's config:
```cpp
// You need these from Person C at server startup:
std::vector<int> ports;              // Which ports to listen on
// Example: [8080, 8081, 9090]

// Per server block:
std::string serverName;              // For virtual hosting (B's problem mostly)
size_t clientMaxBodySize;            // You enforce this during recv()
```

### What to agree on with C:
- **What is the class name?** `ServerConfig`? `Config`? Whatever — just agree.
- **How do you get the list of ports?** `config.getPorts()`? `config.getServerBlocks()`?
- **When do you read it?** Once at startup, or can config be reloaded?

**Recommendation:** Config is read once at startup. You get a `std::vector<ServerConfig>` where each has a port. You create one listening socket per unique port.

---

## 5. The Poll Set — You Own It, But C Needs to Add to It

Your `poll()` call watches all FDs. When Person C starts a CGI process, they create **pipe FDs** that need to go into YOUR poll set. This is a future integration point, but your Phase 1 design must **not make it impossible**.

### What to keep in mind:
- Your poll set management (`addFd()`, `removeFd()`) must be **public or accessible** to Person C
- Don't hardcode "listening FDs" vs "client FDs" as the only two types. CGI pipe FDs are a third type.
- Consider a generic approach:

```cpp
// Good: flexible FD types
enum FdType { LISTEN, CLIENT, CGI_PIPE };

void EventLoop::addFd(int fd, FdType type, short events);
void EventLoop::removeFd(int fd);
```

### Why this matters now
If your Phase 1 event loop is written assuming only 2 FD types (listener + client), Person C can't plug in CGI pipes without rewriting your loop. Design for 3 types from the start.

---

## 6. client_max_body_size — You Enforce It

The subject says clients can't send bodies larger than a configured limit. **You** are the one doing `recv()`, so **you** must enforce this.

### What to keep in mind:
- As you accumulate data in `readBuffer`, check its size
- If `readBuffer.size() > clientMaxBodySize` → stop reading, tell Person B to generate a `413 Payload Too Large` response
- You need the max body size value from Person C's config

### Agree with B:
How do you tell B "this request is too large"? Options:
- You set `client.state = ERROR_413` and B checks that
- You call `Response::makeError(413)` directly
- You just close the connection (simplest but least correct)

---

## 7. Connection States — Define Them Now

Your event loop drives clients through states. Everyone needs to agree on what states exist:

```cpp
enum ClientState {
    READING_REQUEST,    // A: calling recv(), accumulating readBuffer
    PARSING_REQUEST,    // B: parsing readBuffer into Request object
    PROCESSING,         // B: routing, building response (or C: running CGI)
    WRITING_RESPONSE,   // A: calling send(), draining writeBuffer
    WAITING_CGI,        // C: waiting for CGI child process output
    CLOSING             // A: close FD, cleanup
};
```

### Why define now?
If you use integers (0, 1, 2) and B uses strings ("reading", "writing"), you'll clash. Agree on one enum in a shared header.

---

## 8. Quick Summary: Talk to B and C About These

| # | Decision | Talk to | Agree before Phase 1 |
|---|----------|---------|---------------------|
| 1 | Client struct fields | B and C | What fields, who reads/writes each |
| 2 | How B gets raw data | B | Buffer reference? Feed method? |
| 3 | How B returns response | B | Who fills writeBuffer? Who triggers POLLOUT? |
| 4 | Config interface | C | Class name, method to get ports, max body size |
| 5 | Adding FDs to poll set | C | `addFd()`/`removeFd()` must be accessible |
| 6 | Max body size enforcement | B and C | You enforce it, B generates the error response |
| 7 | Client states enum | B and C | Shared enum in a common header |

> [!IMPORTANT]
> **You don't need to implement all of this in Phase 1.** But you need to **design** your Socket and EventLoop classes so that these integrations are possible later. If your Phase 1 code makes any of the above impossible, you'll be rewriting it in Phase 3.

---

## 9. The One Rule

> **Build your Phase 1 code as if Person B and Person C are already done.** Leave the hooks empty, but make sure they exist. A `// TODO: call Person B's parser here` comment in the right place is worth more than a perfect echo server that can't be extended.
