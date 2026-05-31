# Group Multicast Chat — Design & Implementation Notes

This document covers the work done to add **group chat over IP multicast** to the
chat-rooms project, plus the supporting server-side changes and a byte-order bug
fix discovered along the way. It is meant to be read top-to-bottom by someone who
knows C and sockets but hasn't seen this codebase.

Contents:
1. [Big picture](#1-big-picture)
2. [Server side: returning the multicast endpoint](#2-server-side-returning-the-multicast-endpoint)
3. [The IP byte-order bug fix](#3-the-ip-byte-order-bug-fix)
4. [`ActionExit` and session cleanup](#4-actionexit-and-session-cleanup)
5. [Client side: the multi-process architecture](#5-client-side-the-multi-process-architecture)
6. [Threads vs. processes](#6-threads-vs-processes)
7. [The POSIX message queue (PID hand-back)](#7-the-posix-message-queue-pid-hand-back)
8. [Component walkthrough](#8-component-walkthrough)
9. [Configuration](#9-configuration)
10. [Build wiring](#10-build-wiring)
11. [How to build and run](#11-how-to-build-and-run)
12. [Testing](#12-testing)
13. [Known limitations & future work](#13-known-limitations--future-work)
14. [File inventory](#14-file-inventory)

---

## 1. Big picture

The server is a **TCP control plane**. Clients connect over TCP to register, log
in, and create/join/leave groups. The actual *chat messages* do **not** go through
the server — they travel **client-to-client over UDP multicast**. The server's only
job for chat is to act as a **directory**: it assigns each group a multicast
`IP:port` and hands it back to clients.

```
        TCP control plane                     UDP multicast data plane
   ┌─────────┐   create/join    ┌────────┐
   │ client  │ ───────────────► │ server │   (server is NOT in the data path)
   │         │ ◄─────────────── │        │
   └─────────┘  "239.0.0.1:5000"└────────┘
        │
        │ join the group's multicast channel
        ▼
   239.0.0.1:5000  ◄────────────►  other clients in the same group
```

On the client, each joined group is serviced by **two small helper programs**, each
in its own `gnome-terminal` window:
- `chat_sender`   — reads your keyboard input and multicasts it.
- `chat_receiver` — listens on the group and prints incoming messages.

The main client *orchestrates* these windows: it spawns them, learns their PIDs via
a message queue, and kills them when you leave the group / log out / exit.

---

## 2. Server side: returning the multicast endpoint

Each group already had a `GroupEndpoint { m_multicastAddr, m_port }` allocated at
creation (`ServerMng/GroupManager.c::AllocateGroupEndpoint`): the IP is the constant
`CONF_MULTICAST_BASE_IP` (`239.0.0.1`) and the port is `CONF_MULTICAST_PORT_BASE +
index`, so each group gets a distinct port (5000, 5001, …). It was just never sent
to the client.

The change: on a successful `create_group` / `join_group`, the server replies with
the endpoint formatted as the text string `"ip:port"` (e.g. `"239.0.0.1:5000"`) in
the response value, with status `CHAT_OK`. Text was chosen to match the protocol's
existing "all values are NUL-terminated strings" convention, so the existing client
prints it and the new code parses it with `strchr`/`atoi`.

Key pieces:
- `GroupManager_GetGroupEndpoint(manager, name, &endpoint)` — added so the server
  manager can fetch a group's endpoint **by name** without handling a `Group*`
  directly (keeps the `Group` type encapsulated inside `GroupManager`).
- `ServerManager_SendGroupEndpoint(...)` — formats the endpoint via
  `network_convert_ip_n_to_p` + the port and sends it. On a formatting failure it
  now replies `CHAT_ERR_GENERIC` (an earlier version wrongly replied `CHAT_OK` with
  an empty value, which would have falsely signalled success).

No new opcode/status was needed — `OPCODE_RESPONSE` + `CHAT_OK` is reused, and the
existing client already printed `OK:` responses verbatim.

---

## 3. The IP byte-order bug fix

While formatting the endpoint we found that `utils/network_utils.c` had an
inconsistent pair of conversion helpers:

- `network_convert_ip_p_to_n("239.0.0.1")` ran `inet_pton` (which already writes
  **network** byte order) and then applied an **extra `htonl`**, so it actually
  returned **host** byte order — contradicting its name and the `Group.h` comment.
- `network_convert_ip_n_to_p` uses `inet_ntop`, which expects **network** byte
  order.

So the two did **not** round-trip: `n_to_p(p_to_n("239.0.0.1"))` produced
`"1.0.0.239"`. Nothing was *visibly* broken before only because every caller
compensated differently (the client wrapped the result in a second `htonl`; the
server applied `htonl` at `bind()` time). Both `GetIp` header contracts already
promised network byte order, so the **implementations** were what was wrong.

Fix: remove the stray `htonl` from `p_to_n` so the pair becomes a correct inverse,
then drop the now-redundant compensating `htonl`s in `ClientController` and in
`TcpConnectionAcceptor`'s `bind()`. The unit test `test_network_utils.c` was updated
to express the network-order contract endian-portably (e.g.
`p_to_n("192.168.1.1") == htonl(0xC0A80101)`).

This is its own commit (`Fix IP address helpers to use true network byte order`),
separate from the feature work, because it touches the live TCP path and was
regression-tested by confirming a client can still connect to the server.

---

## 4. `ActionExit` and session cleanup

`ServerManager_ActionExit` used to be a stub. It now performs real session
teardown, and the logic is shared so it can't drift:

- `ServerManager_LeaveAllGroups(manager, fd)` — decrements the ref count of every
  group the user is in and removes any that become empty.
- `ServerManager_CleanupSession(manager, record)` — if the fd has a live session,
  leaves all groups and logs the user out. It touches no socket and sends no reply,
  so it is safe to call from the disconnect callback (where the peer is already
  gone).
- `ActionExit` = `CleanupSession` + reply `CHAT_OK "Goodbye"`.
- `ServerManagerCallbackDisconnect` now calls `CleanupSession` too — previously an
  abrupt disconnect leaked group ref counts.

---

## 5. Client side: the multi-process architecture

When the user creates/joins a group and the server returns `"239.0.0.1:5000"`, the
client spawns two windows for that group:

```
  main client (out.client)
        │  system("gnome-terminal -- build/out.chat_receiver 239.0.0.1 5000")
        │  system("gnome-terminal -- build/out.chat_sender   239.0.0.1 5000 alice")
        ▼
  ┌──────────────────┐        ┌──────────────────┐
  │  chat_receiver   │        │   chat_sender    │
  │  (its own window)│        │  (its own window)│
  │  recvfrom loop   │        │  fgets → sendto  │
  └────────┬─────────┘        └─────────┬────────┘
           │ getpid() + role            │ getpid() + role
           └──────────► POSIX mq ◄──────┘
                        /chat_pids
                            │
                            ▼
                   main client mq_receive  →  stores {group → {sender_pid, receiver_pid}}
```

The chat datagrams are plain text `"username: message"`. Each `chat_receiver` on
the group prints whatever it receives; with multicast loopback enabled (the default),
two clients on the *same host* see each other's traffic, which is what makes local
testing work.

---

## 6. Threads vs. processes

**The client side uses no threads at all — concurrency comes from separate
processes.** Every client-side program is single-threaded:

| Process            | Threads | How it waits                              |
|--------------------|:------:|--------------------------------------------|
| `out.client`       | 1      | blocks on `fgets` (menu) / `mq_timedreceive` |
| each `chat_sender` | 1      | blocks on `fgets` (your typing)            |
| each `chat_receiver`| 1     | blocks on `recvfrom` (incoming datagrams)  |

For a user in *N* groups there are `2N + 1` processes, each with exactly one thread.

Why processes instead of a background receive thread? A `chat_receiver` can sit
blocked in `recvfrom` forever without freezing the menu *because it is its own
process* that the kernel schedules independently. The trade-off: separate processes
don't share memory, so they coordinate through **IPC** — the message queue (to send
PIDs up to the parent) and `kill()` (to shut them down). A thread-based design would
share memory but need a `pthread` + careful locking; this design avoids that
entirely. (For contrast, the *server* genuinely is multi-threaded — its
`TcpServerController` runs an acceptor thread and a handler thread — but the client
is not, which is why the client link needs `-lrt` but not `-lpthread`.)

---

## 7. The POSIX message queue (PID hand-back)

### Why it's needed

`system("gnome-terminal -- ./chat_receiver …")` returns the *shell's* exit status,
and gnome-terminal typically hands the command to a shared `gnome-terminal-server`,
so the PID you could get from `system()` is **not** the receiver's PID. To later
`kill()` the right process, each spawned program reports **its own** `getpid()` back
to the client. A message queue is the clean channel for that.

### POSIX vs. System V

We use **POSIX** message queues (`<mqueue.h>`): `mq_open` / `mq_send` /
`mq_receive`, identified by a name (`"/chat_pids"`), linked with `-lrt`. (System V —
`<sys/msg.h>`, `msgget`/`msgsnd`/`msgrcv`, keyed by `ftok` — was the alternative; its
`mtype` field can select messages by type, whereas with POSIX we put a `role` field
in the message body instead.)

### The contract — `ChatIpc.h`

```c
typedef enum { CHAT_ROLE_SENDER = 0, CHAT_ROLE_RECEIVER = 1 } ChatRole;
typedef struct { int m_role; pid_t m_pid; } ChatPidMsg;
void ChatIpc_ReportPid(ChatRole a_role);   /* implemented in ChatIpc.c */
```

`ChatIpc_ReportPid` opens the queue `O_WRONLY`, sends a `ChatPidMsg`, and closes it.
The two key lines:

```c
ChatPidMsg msg = { .m_role = a_role, .m_pid = getpid() };          /* designated init */
mq_send(mq, (const char*)&msg, sizeof(msg), 0);                    /* bytes, len, prio */
```
- `getpid()` is this process's own PID — the value the parent will `kill()`.
- `mq_send` is byte-oriented, hence the `(const char*)&msg` cast; `sizeof(msg)` must
  be `<= mq_msgsize` (set by the client when it creates the queue); `0` is the
  message priority (unused here).
- The struct is sent as a raw blob, so all three programs must be the same ABI —
  fine here since one compiler builds them all.

### `mq_maxmsg` is buffer depth, not a group cap

The queue is created with `mq_maxmsg = CONF_CHAT_PID_QUEUE_MAXMSG (8)`. That is the
max number of **unread** messages the queue can hold — **not** a limit on groups or
windows. In practice at most 2 are ever pending (one sender + one receiver PID)
because the single-threaded client drains them right after each spawn. The value
must stay `<= /proc/sys/fs/mqueue/msg_max` (default 10).

---

## 8. Component walkthrough

### `ChatIpc.{h,c}` (repo root)
Shared type contract + `ChatIpc_ReportPid`. Linked into both `chat_sender` and
`chat_receiver`. The main client only includes the *header* (it receives PIDs, never
reports), so it does not link `ChatIpc.c`.

### `ChatWindows/chat_receiver.c` (argv: `<ip> <port>`)
1. `ChatIpc_ReportPid(CHAT_ROLE_RECEIVER)`.
2. `CreateAndJoinMulticast`: UDP socket → `SO_REUSEADDR` (so multiple receivers can
   bind the same port) → `bind` to `INADDR_ANY:port` → `IP_ADD_MEMBERSHIP` for the
   group IP (the step that makes the kernel deliver the group's datagrams).
3. Loop on `recvfrom`, printing each datagram. The loop exits when `recvfrom` errors
   — which is exactly what happens when the client `kill()`s it, so no stop flag is
   needed.

### `ChatWindows/chat_sender.c` (argv: `<ip> <port> <username>`)
1. `ChatIpc_ReportPid(CHAT_ROLE_SENDER)`.
2. `CreateMulticastSender`: a plain UDP socket + a destination `sockaddr_in` for the
   group. No `bind`/join needed to *send*.
3. Loop on `fgets`, format `"username: text"` (newline stripped, empty lines
   skipped), `sendto` to the group.

### `Client/GroupWindows.{h,c}`
The client-side manager. Owns the PID queue and a `HashMap` of
`group name (strdup'd) → GroupPids { sender, receiver }`.

- `GroupWindows_Create` — `mq_unlink` (clear any stale queue from a crashed run) then
  `mq_open(O_CREAT | O_RDONLY, …)` with `mq_msgsize = sizeof(ChatPidMsg)`. Creating
  it here matters because the windows open it `O_WRONLY` *without* `O_CREAT`.
- `GroupWindows_Open` — closes any existing pair for the group, `SpawnWindow`s the
  receiver then sender via `system("gnome-terminal -- …")`, then `CollectPid` twice
  (`mq_timedreceive` with a `CONF_CHAT_PID_WAIT_SECONDS` timeout, filed by role so
  arrival order doesn't matter), and stores the pair. Every post-spawn failure path
  `KillPids` + frees so nothing leaks.
- `GroupWindows_Close` — `HashMap_Remove`, `KillPids`, free. `NOT_FOUND` if untracked.
- `GroupWindows_CloseAll` — kill every pair, then destroy+recreate the map so it's
  left empty but usable (logout can be followed by another login/join).
- `GroupWindows_Destroy` — kill all, free the map, `mq_close` + `mq_unlink`.

`SpawnWindow` only detects whether *gnome-terminal itself* launched (non-zero/127 if
it's missing); whether the helper program actually started is confirmed by
`CollectPid`'s timeout. `KillPids` sends `SIGTERM` and ignores `ESRCH` (the window
was already closed by hand).

### `Client/ClientApp.c` integration
- Holds a `GroupWindows* m_windows`, created in `ClientApp_Create`, destroyed in
  `ClientApp_Destroy`.
- `ParseEndpoint("ip:port", …)` splits the server's response.
- `HandleGroupAction`: on `CHAT_OK`, **create/join** → `ParseEndpoint` +
  `GroupWindows_Open`; **leave** → `GroupWindows_Close`.
- `HandleLogout` / `HandleExit` → `GroupWindows_CloseAll`.

---

## 9. Configuration

All tunables live in `config.h`:

```c
#define CONF_MULTICAST_BASE_IP   "239.0.0.1"   /* group multicast IP (admin-scoped block) */
#define CONF_MULTICAST_PORT_BASE 5000          /* first group's port; +1 per group */
#define CONF_MULTICAST_MAX_GROUPS 1000
#define CONF_MULTICAST_ENDPOINT_STR_MAX 22     /* "255.255.255.255:65535" + NUL */

#define CONF_CHAT_PID_QUEUE_NAME "/chat_pids"  /* POSIX mq name (must start with '/') */
#define CONF_CHAT_MSG_MAX        512           /* max chat datagram payload (bytes) */
#define CONF_CHAT_PID_QUEUE_MAXMSG 8           /* queue depth (NOT a group cap; <= msg_max) */
#define CONF_CHAT_WINDOWS_MAP_SIZE 16          /* hash buckets for tracked groups */
#define CONF_CHAT_PID_WAIT_SECONDS 5           /* timeout waiting for a window's PID */
#define CONF_CHAT_SPAWN_CMD_MAX 256            /* gnome-terminal command buffer size */
```

Types (the `ChatRole` enum, `ChatPidMsg` struct) stay in `ChatIpc.h`, not `config.h`
— `config.h` holds only literal values and is intentionally include-free.

---

## 10. Build wiring

- `Client/Makefile` — `GroupWindows.c` added to `SRCS`; `-I../db` added for
  `HashMap.h`.
- Top `Makefile`:
  - `CLIENT_LDFLAGS` gains `-Ldb -lDataStructures -lrt` (HashMap + POSIX queue; **no**
    `-lpthread`, the client is single-threaded).
  - Two new executable targets `out.chat_sender` / `out.chat_receiver`, each built
    from its `.c` plus the shared `ChatIpc.c` with `-lrt`.
  - `out.client` now depends on the client archives so editing a `Client/` source
    actually relinks the binary (previously it could go stale).

---

## 11. How to build and run

```bash
make                       # builds out.serverMain, out.client, out.chat_sender, out.chat_receiver

# Terminal A
./build/out.serverMain     # runs until you press Enter

# Terminal B  (run from the repo root!)
./build/out.client
```

In the client menu: **Connect → Register/Login → Create group / Join group**. Two
windows open for the group; type in the sender window, watch the receiver window.
**Leave group** closes that group's windows; **Logout/Exit** closes all of them.

Requirements / gotchas:
- A desktop session with **`gnome-terminal`** (won't work headless or over plain SSH).
- Launch the client **from the repo root** — it spawns `build/out.chat_*` by relative
  path.
- Server and client default to `127.0.0.1:8080` (from `config.h`).

---

## 12. Testing

- **Unit:** `ServerNet/unittests/test_network_utils.c` covers the IP conversion
  round-trip (updated for the byte-order fix).
- **Headless smoke test (no gnome-terminal):** the multicast data path and the PID
  queue were validated by running the *real* `out.chat_sender` / `out.chat_receiver`
  binaries directly (not via gnome-terminal), with a small throwaway `mqtool`
  standing in for the client's queue side. Result: a datagram sent by the sender
  (`"alice: hello from the smoke test"`) was received and printed by the receiver,
  and both programs' PIDs were collected from the queue by role. Teardown left no
  stray processes and no `/dev/mqueue/chat_pids`.
- **GUI test (your desktop):** the parts that *require* gnome-terminal — windows
  appearing on create/join, closing on leave/logout/exit — must be exercised
  interactively. The logic underneath them is covered by the smoke test.

Handy checks while testing:
```bash
ipcs -q 2>/dev/null ; ls /dev/mqueue/      # see/clean POSIX queues
pgrep -x out.chat_receiv ; pgrep -x out.chat_sender   # any stray windows?
```

---

## 13. Known limitations & future work

- **GUI-bound:** requires gnome-terminal + a display.
- **Self-echo:** because multicast loopback is on, your own messages also appear in
  your receiver window. (Intended for local multi-client testing; could be filtered.)
- **Sender-spawn failure** leaves the already-launched receiver window orphaned (it
  only triggers if gnome-terminal disappears between the two `system()` calls).
- **TTL = 1** (default) keeps multicast on the local subnet; bump via
  `IP_MULTICAST_TTL` for cross-subnet.
- **Raw-struct PID message** assumes same ABI for all three programs (true here).

---

## 14. File inventory

New:
- `ChatIpc.h`, `ChatIpc.c` — shared PID-report contract + helper.
- `ChatWindows/chat_receiver.c` — multicast listener window.
- `ChatWindows/chat_sender.c` — multicast sender window.
- `Client/GroupWindows.h`, `Client/GroupWindows.c` — window/queue/PID manager.
- `docs/multicast-chat.md` — this document.

Modified:
- `ServerMng/ServerManager.c`, `ServerMng/GroupManager.{c,h}` — endpoint return +
  `ActionExit`/session cleanup.
- `utils/network_utils.{c,h}`, `ClientNet/ClientController.c`,
  `ServerNet/TcpConnectionAcceptor.c`, `ServerNet/unittests/test_network_utils.c` —
  byte-order fix.
- `Client/ClientApp.c` — GroupWindows integration.
- `config.h` — multicast + chat-IPC constants.
- `Makefile`, `Client/Makefile` — build wiring.
