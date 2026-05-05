# Person A — AI Usage Guide (Short Version)

## The 3 Modes

| Mode | When | Rule |
|------|------|------|
| 🎓 **Learn** | Before coding anything | *"Explain [concept] in the context of my web server"* |
| 🏗️ **Design** | Before writing a class | *"I need to design [X]. Constraints: [list]. What are my options?"* |
| 💻 **Code** | After you understand the concept | *"Write a C++98 function that does [ONE specific thing]"* |

**Always go Learn → Design → Code. Never skip to Code.**

---

## Key Prompts Per Phase

### Phase 1 — Sockets
- *"Explain socket() → bind() → listen() → accept() lifecycle at the OS level"*
- *"Why do I need SO_REUSEADDR?"*
- *"Write a C++98 function that creates a non-blocking TCP listening socket on a given port"*

### Phase 2 — Event Loop
- *"Explain how poll() works. I have 3 listening sockets and 50 clients. What happens in one call?"*
- *"What is the difference between POLLIN, POLLOUT, POLLHUP, POLLERR?"*
- *"Write the main poll() loop body that dispatches to accept/read/write/close handlers"*

### Phase 3 — Integration
- *"Design the interface between my event loop and Person B's HTTP parser. What data do I pass?"*
- *"Write writeToClient() that handles partial sends with non-blocking sockets"*

### Phase 4 — Hardening
- *"How do I detect file descriptor leaks?"*
- *"Give me siege/ab commands to stress test 100 concurrent connections"*

---

## After AI Gives You Code — Check These

1. ✅ Can I explain every line?
2. ✅ Is it C++98? (no `auto`, `nullptr`, range-for, lambdas)
3. ✅ Does `make` pass with `-Wall -Wextra -Werror -std=c++98`?
4. ✅ Are all FDs closed on every error path?
5. ✅ Does it handle `EAGAIN` on recv/send/accept?
6. ✅ Can I explain it to my teammate?

**If any answer is no → go back to Learn Mode.**

---

## Golden Rules

| Do ✅ | Don't ❌ |
|-------|---------|
| Ask for **one function** at a time | Ask for "the entire Server class" |
| **Type it yourself** — don't copy-paste | Paste AI code without reading it |
| **Break the code on purpose** to learn | Trust AI blindly |
| Explain it back in your own words | Move on if you don't understand |
| Always try first, ask AI when stuck | Ask AI before attempting |
| Practice explaining without AI | Use AI during mock evaluation |

---

## Debugging Prompt Template

```
Here is my code: [paste]
Expected: [what should happen]
Actual: [what happens instead]
I already tried: [what you checked]
What am I missing?
```
