# Webserv — Git Workflow Guide (Simple: main + 3 Branches)

---

## 1. Branching Strategy

### 4 Branches Total — That's It

```
main ──────────────────────────────────── (stable, always compiles)
  │
  ├── person-a ────────────────────────── Person A works here (server/networking)
  │
  ├── person-b ────────────────────────── Person B works here (HTTP protocol)
  │
  └── person-c ────────────────────────── Person C works here (config/CGI)
```

| Branch | Purpose | Rule |
|--------|---------|------|
| **`main`** | Stable, tested code. Always compiles. | Only merge into it — never code directly on it |
| **`person-a`** | All of Person A's work | Only Person A pushes here |
| **`person-b`** | All of Person B's work | Only Person B pushes here |
| **`person-c`** | All of Person C's work | Only Person C pushes here |

> [!TIP]
> Replace `person-a`, `person-b`, `person-c` with actual names or 42 logins (e.g., `aysadeq`, `john`, `sarah`).

---

## 2. Initial Setup (Do Once)

### One Person Creates the Branches
```bash
git checkout main
git checkout -b person-a
git push -u origin person-a

git checkout main
git checkout -b person-b
git push -u origin person-b

git checkout main
git checkout -b person-c
git push -u origin person-c
```

### Each Person Clones and Goes to Their Branch
```bash
git clone <repo-url> Webserv
cd Webserv
git checkout person-a    # (or person-b, person-c)
```

### Set Up `.gitignore` (One Person, on `main`)
```bash
git checkout main

cat > .gitignore << 'EOF'
# Compiled
webserv
*.o
*.d

# Editor
.vscode/
.idea/
*.swp
*.swo
*~
.DS_Store

# Debug
*.dSYM/
EOF

git add .gitignore
git commit -m "chore: add .gitignore"
git push origin main
```

Then everyone pulls it:
```bash
git checkout person-a
git merge main
```

---

## 3. The Daily Workflow

### Your Daily Cycle (4 Steps)

```
1. PULL main     → Get teammates' merged work
2. CODE          → Write code on your branch
3. COMMIT + PUSH → Save your progress
4. MERGE to main → When your feature is ready and tested
```

---

### Step 1: Start Your Day — Get Latest `main`

```bash
git checkout person-a          # Your branch
git pull origin person-a       # Get any remote changes on your branch
git merge origin/main          # Bring in teammates' latest merged work
```

This ensures you're building on top of everyone's latest stable code.

### Step 2: Write Code, Compile, Test

```bash
# Edit your files (srcs/server/*.cpp, srcs/server/*.hpp)
make
./webserv config/default.conf
# Test...
```

### Step 3: Commit and Push (Do This Often!)

```bash
# Check what changed
git status
git diff

# Stage your changes
git add srcs/server/Socket.cpp srcs/server/Socket.hpp

# Commit with a clear message
git commit -m "feat: implement Socket class with bind and listen"

# Push to your remote branch
git push origin person-a
```

> [!IMPORTANT]
> **Commit often, push daily.** Don't accumulate 3 days of uncommitted work — if your laptop dies, it's all gone.

### Step 4: Merge to `main` (When Feature Is Ready)

```bash
# Make sure your code compiles and works
make re
./webserv config/default.conf

# Get latest main
git checkout main
git pull origin main

# Merge your branch into main
git merge person-a

# If conflicts → resolve them (see Section 5)

# Push the updated main
git push origin main

# Go back to your branch
git checkout person-a
```

**Tell the team:** "I just merged to main — pull when you can."

---

## 4. How the Flow Looks Over Time

```
main:       M──────────────M₁─────────────────M₂─────────────M₃──────
             \            ↗                   ↗              ↗
person-a:     A──a──a──a─╱───a──a──a──a──a──a╱──a──a──────a╱───a──
              \         ↗                   ↗              ↗
person-b:      B──b──b─╱──b──b──────b──b──b╱──b──b──b──b╱────b──
               \       ↗                 ↗              ↗
person-c:       C──c──╱──c──c──c──c──c──╱──c──c──────c╱──────c──

M₁ = Checkpoint 1 (end of week 2): all 3 merge into main
M₂ = Checkpoint 2 (end of week 3): all 3 merge into main
M₃ = Checkpoint 3 (end of week 4): all 3 merge into main
```

---

## 5. Handling Merge Conflicts

### When Do They Happen?
When two people edit the **same lines** in the **same file**. Common conflict spots:

| File | Why |
|------|-----|
| `srcs/main.cpp` | All 3 may add includes or init code |
| `Makefile` | Adding new source files to the build |
| Shared headers | If someone changes an interface |

### What It Looks Like
```cpp
<<<<<<< HEAD
// What's already in main
Server server(config);
=======
// What you're merging in
Server server(configParser.getConfig());
>>>>>>> person-a
```

### How to Fix

```bash
# 1. Git tells you which files conflict
git status
# "both modified: srcs/main.cpp"

# 2. Open the file, find <<<<<<< markers
# 3. Decide what the correct code should be
# 4. Remove ALL conflict markers (<<<, ===, >>>)
# 5. Stage and commit

git add srcs/main.cpp
git commit -m "fix: resolve merge conflict in main.cpp"
git push origin main
```

> [!WARNING]
> **Talk to the other person before resolving.** Don't just keep your version and delete theirs.

---

## 6. Essential Commands Cheat Sheet

### Daily Commands

| What You Want | Command |
|---------------|---------|
| See what changed | `git status` |
| See the actual changes | `git diff` |
| Stage a file | `git add <file>` |
| Stage a whole folder | `git add srcs/server/` |
| Commit | `git commit -m "feat: description"` |
| Push your branch | `git push origin person-a` |
| Switch branches | `git checkout main` or `git checkout person-a` |
| Get latest main | `git pull origin main` |
| Merge main into your branch | `git merge origin/main` |
| See commit history | `git log --oneline -10` |
| Visual branch graph | `git log --oneline --graph --all -20` |

### Occasional Commands

| What You Want | Command |
|---------------|---------|
| Undo unstaged changes to a file | `git checkout -- <file>` |
| Unstage a file (keep changes) | `git reset HEAD <file>` |
| Undo last commit (keep changes) | `git reset --soft HEAD~1` |
| Save work temporarily | `git stash` |
| Restore saved work | `git stash pop` |
| Tag a milestone | `git tag checkpoint-1` |
| Push a tag | `git push origin checkpoint-1` |

---

## 7. Commit Message Convention

### Format
```
<type>: <short description>
```

### Types

| Type | When | Example |
|------|------|---------|
| `feat` | New feature | `feat: implement event loop with poll()` |
| `fix` | Bug fix | `fix: close client FD on disconnect` |
| `refactor` | Restructure code | `refactor: split Request parsing into methods` |
| `docs` | Documentation | `docs: add compilation steps to README` |
| `chore` | Build/config | `chore: add new source files to Makefile` |
| `test` | Test files | `test: add Python CGI test script` |

---

## 8. The 3 Scenarios You'll Face

### "I'm starting my day"
```bash
git checkout person-a
git pull origin person-a
git merge origin/main
# Now code...
```

### "I finished a feature, time to merge"
```bash
make re                         # Make sure it compiles
git add -A
git commit -m "feat: complete event loop"
git push origin person-a

git checkout main
git pull origin main
git merge person-a
git push origin main

git checkout person-a           # Go back to your branch
```

### "Teammate merged, I need their code"
```bash
git checkout person-a
git pull origin main
git merge origin/main
# Their changes are now in your branch
```

---

## 9. Team Rules

| # | Rule |
|---|------|
| 1 | **Never code directly on `main`** — always work on your branch |
| 2 | **Never force push** (`git push -f`) — it destroys history |
| 3 | **Always `make re` before merging to `main`** — broken main = broken team |
| 4 | **Merge to `main` together at checkpoints** — Person A first, then B pulls and merges, then C |
| 5 | **Announce merges** — "Merging to main now" in your group chat |
| 6 | **Commit often, push daily** — small commits are easier to debug |
| 7 | **Don't touch other people's files** — stay in your folders (`srcs/server/`, `srcs/http/`, etc.) |

---

## 10. Checkpoint Merge Order

When all 3 merge at a checkpoint, do it **one at a time** to avoid chaos:

```
1. Person A merges to main → pushes → tells the team
2. Person B pulls main → resolves any conflicts → merges to main → pushes
3. Person C pulls main → resolves any conflicts → merges to main → pushes
4. Everyone pulls main → merges main into their branch → tests
```

> [!TIP]
> **After a checkpoint merge, everyone should run `make re` and test the full server.** If something broke, fix it together before continuing.
