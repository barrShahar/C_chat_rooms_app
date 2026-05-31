# Server Side — Architecture & Implementation Notes

This document describes the **server half** of the chat-rooms project: how a client
connection turns into a registered, logged-in user; how groups are created and torn
down; and how the networking, threading, and protocol layers fit together. It is
meant to be read top-to-bottom by someone who knows C and BSD sockets but hasn't
seen this codebase.

For the *chat data plane* (UDP multicast, the spawned `chat_sender` /
`chat_receiver` windows, the PID message queue), see
[`multicast-chat.md`](./multicast-chat.md). This document stays on the server.

Contents:
1. [Big picture](#1-big-picture)
2. [Layer map & ownership](#2-layer-map--ownership)
3. [The networking layer (`ServerNet`)](#3-the-networking-layer-servernet)
4. [Threading model](#4-threading-model)
5. [The management layer (`ServerMng`)](#5-the-management-layer-servermng)
6. [The wire protocol](#6-the-wire-protocol)
7. [Request dispatch: the action table](#7-request-dispatch-the-action-table)
8. [Action walkthroughs](#8-action-walkthroughs)
9. [Session lifecycle & cleanup](#9-session-lifecycle--cleanup)
10. [Multicast endpoint allocation](#10-multicast-endpoint-allocation)
11. [Result/error vocabulary](#11-resulterror-vocabulary)
12. [Configuration](#12-configuration)
13. [Build & run](#13-build--run)
14. [Known limitations & future work](#14-known-limitations--future-work)
15. [File inventory](#15-file-inventory)

---

## 1. Big picture

The server is a **TCP control plane**. Clients connect over TCP to register, log
in, and create/join/leave groups. The actual chat messages do **not** flow through
the server — see [`multicast-chat.md`](./multicast-chat.md). On the server side, the
job is:

- Accept TCP connections and frame inbound bytes into protocol messages.
- Maintain the set of registered users and their group memberships.
- Maintain the set of live groups, each with an allocated multicast `IP:port`.
- Answer each request with a single framed `OPCODE_RESPONSE` reply.

The codebase splits cleanly into two layers, each its own directory and static
library:

```
   serverMain.c
        │  creates / starts / stops
        ▼
   ┌──────────────────────────────────────────────┐
   │ ServerMng (application / domain logic)         │
   │   ServerManager  ── UserManager  ── User       │
   │                  └─ GroupManager ── Group      │
   └──────────────────────────────────────────────┘
        │  registers callbacks, calls SendMessage
        ▼
   ┌──────────────────────────────────────────────┐
   │ ServerNet (transport)                          │
   │   TcpServerController                          │
   │     ├─ TcpConnectionAcceptor  (accept loop)    │
   │     └─ TcpConnectionHandler   (select I/O loop)│
   │   TcpConnectionRecord  (per-connection data)   │
   └──────────────────────────────────────────────┘
```

`ServerNet` knows nothing about users, groups, or the chat protocol — it deals only
in file descriptors and byte buffers. `ServerMng` knows nothing about sockets,
`accept()`, or `select()` — it receives decoded buffers via callbacks and replies by
calling back into the controller. The seam between them is three function-pointer
callbacks plus `TcpServerController_SendMessage`.

---

## 2. Layer map & ownership

`serverMain.c` is deliberately tiny. It creates a `ServerManager`, starts it, blocks
on `getchar()`, then stops and destroys it:

```c
ServerManager* serverManager = ServerManager_Create(CONF_SERVER_NAME, CONF_SERVER_IP, CONF_SERVER_PORT);
ServerManager_Start(serverManager);
getchar();                          /* block the main thread until the operator hits Enter */
ServerManager_Stop(serverManager);
ServerManager_Destroy(&serverManager);
```

Ownership is strictly hierarchical, and every `_Destroy` nulls the caller's pointer:

- `ServerManager` **owns** one `UserManager`, one `GroupManager`, and one
  `TcpServerController`. Created in dependency order in `ServerManager_Create`;
  destroyed in reverse (controller first, so worker threads stop touching the
  managers before the managers are freed).
- `TcpServerController` **owns** one `TcpConnectionAcceptor` and one
  `TcpConnectionHandler` (see the class diagram in
  [`ServerNet/class_diagram.mmd`](../ServerNet/class_diagram.mmd)).
- `TcpConnectionHandler` **owns** the live list of `TcpConnectionRecord`s.
- `UserManager` **owns** its `User` objects; each `User` **owns** its list of group
  names. `GroupManager` **owns** its `Group` objects.

The acceptor and handler each hold a non-owning back-reference to the controller so
they can fire callbacks and call `SendMessage`.

---

## 3. The networking layer (`ServerNet`)

Three concrete types plus a value object:

**`TcpConnectionRecord`** (`TcpConnectionRecord.h`) — the per-connection value
object that flows through the whole system:

```c
typedef struct TcpConnectionRecord {
    int  m_fdConnection;   /* the accepted socket fd — the de-facto session key */
    char m_ip[16];         /* peer dotted-quad */
    int  m_port;           /* peer port */
} TcpConnectionRecord;
```

The **`m_fdConnection` is the session identity** everywhere in `ServerMng` — users
are keyed by fd, not by username, while connected.

**`TcpConnectionAcceptor`** (`TcpConnectionAcceptor.c`) — owns the listening socket
and runs an `accept()` loop on its own thread. For each accepted connection it
builds a `TcpConnectionRecord` and hands it to the controller, which forwards it to
the handler.

**`TcpConnectionHandler`** (`TcpConnectionHandler.c`) — runs a single `select()`-based
I/O loop on its own thread, multiplexing all client sockets plus a self-pipe. It
owns the connection list and the `fd_set`. When a socket becomes readable it reads
the bytes and fires the message-received callback; on EOF/error it fires the
disconnect callback and drops the connection.

**`TcpServerController`** (`TcpServerController.c`) — the façade. It creates and owns
the acceptor and handler, stores the three callbacks plus an opaque context pointer
(the `ServerManager*`), and exposes `SendMessage(fd, buf, len)` so the upper layer
can reply. Lifecycle calls (`Create`/`Start`/`Stop`/`SetCallbacks`/`Destroy`) are
serialized internally by the controller's mutex.

The callback contract (from `TcpServerController.h`):

```c
TcpServerController_SetCallbacks(controller, context,
    onNewConnection,    /* (context, record)                       */
    onDisconnect,       /* (context, record)                       */
    onMessageReceived); /* (context, record, message, length)      */
```

Must be called **before** `Start`; calling it on a running server returns
`TCP_RESULT_INVALID_ARGUMENT`. Any callback may be `NULL` to disable it.

---

## 4. Threading model

This is the part most likely to bite a future reader, so it is spelled out in the
headers too. There are **three threads** in play:

| Thread | Spawned by | Responsibility |
|--------|-----------|----------------|
| **Owner / main** | the process | lifecycle: `Create`/`Start`/`Stop`/`Destroy`; blocks on `getchar()` |
| **Acceptor worker** | `TcpConnectionHandler`'s sibling, via `Start` | blocks in `accept()`, builds records, pushes them to the handler |
| **Handler worker** | `Start` | the `select()` loop; sole owner of the connection list and `fd_set` after `Start` |

Key rules:

- **All three `ServerManager`/controller callbacks run on the handler worker
  thread**, never on the thread that called `Start`. So `ServerMng`'s action
  handlers all execute on that one worker thread — which is why the managers
  currently need no internal locking: requests for a given (and every) connection
  are processed serially by a single thread.
- New connections are handed from the acceptor to the handler through the
  **self-pipe trick**: a `pipe(2)` write of `sizeof(void*)` bytes is atomic per
  POSIX, so the acceptor can shove a `TcpConnectionRecord*` through it with no lock.
  The handler sees `pipe[0]` become readable inside `select()` and reads the pointer
  back out.
- A **`NULL` pointer pushed through the pipe doubles as the stop signal** from
  `Stop()`, which is how the handler's blocking `select()` is woken for shutdown.
- The acceptor is unblocked on stop via `shutdown(m_listenFd)`; the fd is only
  `close()`d **after** `pthread_join`, so a worker never reads a closed/recycled fd.
- `m_state` flags in both workers are `_Atomic` (seq-cst) so the loop's read is a
  well-defined cross-thread observation.

The notify-new-connection callback is deliberately fired **by the handler** (not the
acceptor) so it runs on the same thread as the message/disconnect callbacks and the
record is guaranteed alive — see `TcpServerController_NotifyNewConnection`.

---

## 5. The management layer (`ServerMng`)

### `ServerManager`

The orchestrator. It wires the three controller callbacks to its own static
functions and holds the `UserManager` + `GroupManager`:

```c
struct ServerManager {
    UserManager*         m_userManager;
    GroupManager*        m_groupManager;
    TcpServerController*  m_tcpServerController;
};
```

- `ServerManagerCallbackNewConnection` — just logs the peer.
- `ServerManagerCallbackDisconnect` — runs `ServerManager_CleanupSession` (leave all
  groups + logout), since a dropped TCP connection must not leave a phantom session.
- `ServerManagerCallbackRecv` — deserializes the buffer into a `ChatMessage`,
  looks up the opcode in the **action table**, and dispatches.

### `UserManager` / `User`

`UserManager` (`UserManager.c`, header `UserManager.h`) is a hash map of users plus
the operations the protocol needs: `AddUser`, `Login`, `Logout`, `AddUserToGroup`,
`RemoveUserFromGroup`, group enumeration (`GetUserGroupCount` / `GetUserGroups`), and
list-formatting helpers (`FormatAllUsersAndGroups`). Users are addressed by
`fdConnection`. A `User` (`User.c`) holds username, password, an online/offline
`UserState`, the current fd, and a list of the group names it belongs to.

The group-enumeration API is split into a **count-then-fill** pair so the caller can
stack-allocate exactly the right array:

```c
size_t n = 0;
UserManager_GetUserGroupCount(um, fd, &n);
char* names[n];                                   /* VLA sized from the count   */
UserManager_GetUserGroups(um, fd, (const char**)names, n, &written);
```

The strings written into the array are **borrowed** (owned by the groups) — the
caller frees the array, never the strings.

### `GroupManager` / `Group`

`GroupManager` (`GroupManager.c`) is a hash map of `Group` objects keyed by name. A
`Group` (`Group.h`) holds its name, its multicast `GroupEndpoint`, and a
**reference count** of how many users are using the channel:

```c
typedef struct GroupEndpoint {
    uint32_t m_multicastAddr;  /* network byte order */
    uint16_t m_port;           /* host byte order     */
} GroupEndpoint;
```

The ref count drives teardown policy: create and join `IncreaseGroupRefCount`; leave
and logout `DecreaseGroupRefCount`; `RemoveGroupIfEmpty` deletes a group only once
its count hits 0. This is how an empty group is reclaimed without an explicit
"delete group" command.

Both managers take the **same hash + equality functions** at construction:
`ServerManagerHashFunctionDJB2` (the classic DJB2 string hash) and a `strcmp`-based
equality, both defined at the bottom of `ServerManager.c`.

---

## 6. The wire protocol

Defined in [`NetworkProtocol.h`](../NetworkProtocol.h) as a header-only TLV codec.
Every message on the wire is:

```
[ Opcode : 1 byte ][ Length : uint16 little-endian ][ Status : 1 byte ][ Value : Length bytes ]
└──────────────────────── 4-byte header (CHAT_HEADER_SIZE) ───────────────────────┘
```

- `Value` is at most `CHAT_MAX_VALUE` (256) bytes; total at most `CHAT_MAX_TOTAL`.
- The **Status** byte is only meaningful on responses; on requests it is ignored.
- `Length` is encoded **little-endian** by hand (`buf[1] = len & 0xFF; buf[2] = len >> 8`).

Opcodes (`MessageOpcode`) — requests are `0x0x`, the single response type is `0x81`:

| Opcode | Value encoding |
|--------|----------------|
| `OPCODE_REGISTER` / `OPCODE_LOGIN` | `"name\0password\0"` |
| `OPCODE_CREATE_GROUP` / `JOIN_GROUP` / `LEAVE_GROUP` | `"group_name\0"` |
| `OPCODE_LOGOUT` / `OPCODE_EXIT` | empty (`Length = 0`) |
| `OPCODE_LIST_USERS` / `OPCODE_LIST_GROUPS` | empty (`Length = 0`) |
| `OPCODE_RESPONSE` | status byte + `"message\0"`; for the LIST replies the message is a newline-separated list, e.g. `"alice\nbob\ncarol\0"` |

The two name+password requests pack both strings into the value with a NUL between
them; the server splits them with pointer arithmetic:

```c
const char* username = (const char*)a_message->m_value;
const char* password = username + strlen(username) + 1;
```

Key functions: `SerializeChatMessage` (struct → buffer, returns byte count or `-1`),
`DeserializeChatMessage` (buffer → struct, validates length against
`CHAT_MAX_VALUE`), and the `Chat_Get*` header accessors. `Chat_Validate` is the
single guard that rejects oversized or truncated frames.

> **Note** — the protocol header has two enum aliases colliding on value `9`
> (`CHAT_ERR_NOT_IN_GROUP` and `CHAT_ERR_MALFORMED`). See
> [§14](#14-known-limitations--future-work).

---

## 7. Request dispatch: the action table

Dispatch was refactored from a `switch` into a **table of function pointers** (see
`0001RefactorServerManageractiondispatchintoatable.patch`). The table lives at the
top of `ServerManager.c`:

```c
typedef void (*ActionFn)(ServerManager*, const TcpConnectionRecord*, const ChatMessage*);

typedef struct { MessageOpcode m_opcode; const char* m_name; ActionFn m_fn; } ActionEntry;

static const ActionEntry s_actions[] = {
    { OPCODE_REGISTER,     "register",     ServerManager_ActionRegister     },
    { OPCODE_LOGIN,        "login",        ServerManager_ActionLogin        },
    { OPCODE_LOGOUT,       "logout",       ServerManager_ActionLogout       },
    { OPCODE_EXIT,         "exit",         ServerManager_ActionExit         },
    { OPCODE_CREATE_GROUP, "create_group", ServerManager_ActionCreateGroup  },
    { OPCODE_JOIN_GROUP,   "join_group",   ServerManager_ActionJoinGroup    },
    { OPCODE_LEAVE_GROUP,  "leave_group",  ServerManager_ActionLeaveGroup   },
    { OPCODE_LIST_USERS,   "list_users",   ServerManager_ActionListUsers    },
    { OPCODE_LIST_GROUPS,  "list_groups",  ServerManager_ActionListGroups   },
};
```

The receive callback does the whole pipeline:

```
recv buffer ──► DeserializeChatMessage ──► FindAction(opcode) ──► action->m_fn(...)
                     │ fail                      │ NULL
                     ▼                           ▼
            reply CHAT_ERR_MALFORMED     reply CHAT_ERR_GENERIC + log "Unhandled opcode"
```

Adding a new request is a two-liner: add an `OPCODE_*` to `NetworkProtocol.h`, write
a `ServerManager_ActionXxx`, and add one row to `s_actions`. `m_name` exists purely
for the `LOG_DEBUG("Dispatching action: %s", ...)` line.

Every action takes the **same signature** `(manager, record, message)` and replies
exactly once via the `ServerManager_SendOrLog` helper.

---

## 8. Action walkthroughs

All replies go through `ServerManager_SendOrLog(record, status, message, length)`,
which serializes an `OPCODE_RESPONSE` and `send()`s it on the connection's fd,
logging (but not surfacing) any send failure.

- **Register** — `UserManager_AddUser(fd, name, pwd)`. Failure → `CHAT_ERR_BAD_CREDS`
  with the manager result string; success → `CHAT_OK`.
- **Login** — `UserManager_Login`. Distinguishes `ALREADY_LOG`
  (`CHAT_ERR_ALREADY_LOGGED_IN`) from other failures (`CHAT_ERR_BAD_CREDS`); success
  → `CHAT_OK` with `"Login successful"`.
- **Logout** — asserts the fd is logged in, then `LeaveAllGroups` + `Logout`. Replies
  `CHAT_OK "Logout successful"`. The returned username is freed (or substituted with
  the peer IP for logging if the manager returned none).
- **Exit** — runs `CleanupSession` and replies `CHAT_OK "Goodbye"`. The actual socket
  teardown is driven by the client closing the connection, which then fires the
  disconnect callback.
- **CreateGroup** — `GroupManager_AddGroup` (allocates the endpoint), then
  `UserManager_AddUserToGroup` for the creator. **On the second step failing it rolls
  back** by removing the just-created empty group, so a half-created group never
  lingers. On success it bumps the ref count and replies with the endpoint (see
  next).
- **JoinGroup** — verifies the group exists and the user is logged in, adds the group
  to the user, bumps the ref count, and replies with the endpoint.
- **LeaveGroup** — verifies group exists / user logged in / user actually in the
  group, removes the membership, decrements the ref count, and `RemoveGroupIfEmpty`.
  Replies `CHAT_OK` (empty body).
- **ListUsers** — logs the full dump server-side, then `FormatAllUsersAndGroups` into
  a `CHAT_MAX_VALUE` buffer and replies with `strlen+1` (not the buffer capacity).
- **ListGroups** — `FormatGroupList` into a wire-sized buffer, replies with the exact
  string length. Both list formatters **truncate to fit**, so a wire-sized buffer is
  always safe and the empty case yields an empty string.

The endpoint reply is built by `ServerManager_SendGroupEndpoint`: it fetches the
`GroupEndpoint`, converts the address to dotted-quad with
`network_convert_ip_n_to_p`, formats `"ip:port"`, and replies `CHAT_OK` with the
string (NUL included). The client uses this to join the group's UDP multicast
channel.

---

## 9. Session lifecycle & cleanup

A "session" is the (user logged in on a given fd) tuple. There are **three exits**
out of a session and they share one helper:

1. **Explicit logout** (`OPCODE_LOGOUT`) — replies, then leaves groups + logs out.
2. **Explicit exit** (`OPCODE_EXIT`) — `CleanupSession`, replies `"Goodbye"`; the
   client then closes the socket.
3. **Abrupt disconnect** — the handler detects EOF and fires the disconnect callback,
   which calls `CleanupSession`.

`ServerManager_CleanupSession` is the single safe teardown path:

```c
if (!isLoggedIn) return;            /* a client may connect/exit without logging in */
ServerManager_LeaveAllGroups(...);  /* dec ref count of each group, remove if empty   */
UserManager_Logout(...);            /* flip state to OFFLINE, return the username      */
```

It **does not touch the socket and does not reply**, so it is safe to call from the
disconnect callback where the peer is already gone. `LeaveAllGroups` is the shared
"decrement every group's ref count and reclaim the empty ones" routine used by both
logout and cleanup; it tolerates a user that is in no groups.

This is what guarantees a crashed or rudely-disconnected client doesn't leak group
references and keep a channel alive forever.

---

## 10. Multicast endpoint allocation

Each group gets a multicast `IP:port` at creation, in
`GroupManager.c::AllocateGroupEndpoint`:

- **IP** is the constant `CONF_MULTICAST_BASE_IP` (`239.0.0.1`, from the
  administratively-scoped `239.0.0.0/8` block), stored in **network byte order** via
  `network_convert_ip_p_to_n`.
- **Port** is `CONF_MULTICAST_PORT_BASE + index`, where `index` is the current group
  count — i.e. ports are handed out sequentially from `5000`.
- Allocation refuses once `index >= CONF_MULTICAST_MAX_GROUPS` (1000).

> Because `index` is derived from the live group count rather than a monotonically
> increasing counter, port reuse after group removal is possible — see
> [§14](#14-known-limitations--future-work).

---

## 11. Result/error vocabulary

Each layer has its own result enum, and the `ServerMng` enums are generated from
X-macro tables via `enum_helper.h` (`DEFINE_ENUM` + `DEFINE_ENUM_TO_STRING`), so the
`*_toString` converters never drift from the enum:

| Enum | Defined in | `ToString` |
|------|-----------|------------|
| `TcpResult` | `TcpServerController.h` | `TcpResult_ToString` (hand-written) |
| `ServerResult` | `ServerManager.h` | `ServerResult_ToString` (hand-written switch) |
| `UserManagerResult` | `UserManager.h` | `UserManagerResult_toString` (generated) |
| `GroupManagerResult` | `GroupManager.h` | `GroupManagerResult_toString` (generated) |
| `UserResult` / `UserState` | `User.h` | — |
| `ChatStatus` | `NetworkProtocol.h` | — (the byte sent on the wire) |

The action handlers translate a manager result into a `ChatStatus` for the wire and
also pass the manager result *string* as the human-readable message body, which is
how the client surfaces specific failures.

---

## 12. Configuration

All compile-time knobs live in [`config.h`](../config.h):

| Macro | Default | Meaning |
|-------|---------|---------|
| `CONF_SERVER_IP` / `CONF_SERVER_PORT` | `127.0.0.1` / `8080` | TCP bind address |
| `CONF_SERVER_NAME` | `"Chat rooms server"` | controller name (logging) |
| `CONF_RECV_BUF_SIZE` | `4096` | handler receive buffer |
| `CONF_GROUP_MANAGER_HASH_MAP_SIZE` | `10` | group hash buckets |
| `CONF_GROUP_MANAGER_MAX_NAME_LENGTH` | `32` | max group name length |
| `CONF_USER_MANAGER_HASH_MAP_SIZE` | `10` | user hash buckets |
| `CONF_USER_GROUPS_LINE_BUF_SIZE` | `256` | per-user line buffer for the list dump |
| `CONF_MULTICAST_BASE_IP` | `239.0.0.1` | base multicast address |
| `CONF_MULTICAST_PORT_BASE` | `5000` | first multicast port |
| `CONF_MULTICAST_MAX_GROUPS` | `1000` | cap on simultaneous groups |
| `CONF_MULTICAST_ENDPOINT_STR_MAX` | `22` | `"ip:port\0"` buffer size |

(The `CONF_CHAT_*` macros belong to the client's chat-window machinery and are
documented in [`multicast-chat.md`](./multicast-chat.md).)

---

## 13. Build & run

The server links the two static libraries from `ServerNet` and `ServerMng` plus the
shared `utils` and `db` helpers. From the repo root:

```bash
make            # builds the server (and other targets)
./build/out.serverMain
```

The server binds to `127.0.0.1:8080`, starts the acceptor + handler threads, and
blocks on `getchar()`. Press **Enter** in its terminal to trigger the orderly
`Stop` → `Destroy` shutdown (which joins both worker threads before freeing
anything). Logs go through the `logger.h` macros (`LOG_INFO`/`LOG_DEBUG`/...).

To exercise it without the full client, the protocol is simple enough to drive by
hand-crafting TLV frames over a TCP socket; the unit tests under
`ServerNet/unittests` and `tests/test_network_protocol.c` are the best starting
references.

---

## 14. Known limitations & future work

These are observations from reading the code, not necessarily bugs to fix today:

- **`ChatStatus` value collision.** `CHAT_ERR_NOT_IN_GROUP` and `CHAT_ERR_MALFORMED`
  are both `9` in `NetworkProtocol.h`; the client cannot distinguish them. They
  should get distinct values.
- **Multicast port reuse.** `AllocateGroupEndpoint` derives the port from the *live
  group count*, so after a group is removed a future group can be assigned the same
  port — fine when groups are short-lived, but not collision-proof. A monotonic
  counter or a free-list would be more robust.
- **Single-threaded request processing.** All actions run on the one handler worker
  thread, so the managers need no locks today. If request handling is ever moved to a
  thread pool, `UserManager`/`GroupManager` will need synchronization.
- **`SOFT_ASSERT` paths.** Logout and the endpoint reply assert invariants that
  "can't happen"; in a release build these degrade to an error reply rather than a
  crash, which is intentional but worth knowing when debugging.
- **TODO in Register.** `ServerManager_ActionRegister` has a `TODO: Handle this
  error` for the add-user failure path; it currently maps every failure to
  `CHAT_ERR_BAD_CREDS`.

---

## 15. File inventory

| File | Role |
|------|------|
| `serverMain.c` | entry point: create/start/stop/destroy the `ServerManager` |
| `config.h` | all compile-time configuration |
| `NetworkProtocol.h` | header-only TLV wire codec, opcodes, statuses |
| **`ServerMng/`** | |
| `ServerManager.{c,h}` | orchestrator, callbacks, action table, send helpers |
| `UserManager.{c,h}` | user registry, login/logout, group membership |
| `User.{c,h}` | a single user (state, fd, password, group-name list) |
| `GroupManager.{c,h}` | group registry, endpoint allocation, ref counting |
| `Group.{c,h}` | a single group (name, endpoint, ref count) |
| **`ServerNet/`** | |
| `TcpServerController.{c,h}` | transport façade; owns acceptor + handler; `SendMessage` |
| `TcpConnectionAcceptor.{c,h}` | `accept()` loop thread |
| `TcpConnectionHandler.{c,h}` | `select()` I/O loop thread; owns the connection list |
| `TcpConnectionRecord.{c,h}` | per-connection value object (fd, ip, port) |
| `class_diagram.mmd` | Mermaid class diagram of the `ServerNet` layer |
| **`utils/`** | `network_utils` (byte-order/IP helpers), `logger.h`, `enum_helper.h` |

---

*Companion document:* [`multicast-chat.md`](./multicast-chat.md) covers the UDP data
plane and the client-side chat windows.
