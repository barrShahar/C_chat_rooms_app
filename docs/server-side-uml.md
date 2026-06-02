# Server Side — UML & Sequence Diagrams

This document gives a structural (UML class), behavioural (threading + group
lifecycle), and interaction (sequence) view of the **server half** of the chat-rooms
project. It is the diagram-focused companion to [`server-side.md`](./server-side.md)
(prose architecture) and mirrors [`client-side.md`](./client-side.md) on the client.

The diagrams are written in [Mermaid](https://mermaid.js.org/); GitHub renders them
inline.

Contents:
1. [Component overview](#1-component-overview)
2. [Class diagram](#2-class-diagram)
3. [Threading model](#3-threading-model)
4. [Sequence: startup](#4-sequence-startup)
5. [Sequence: accepting a connection (acceptor → handler)](#5-sequence-accepting-a-connection-acceptor--handler)
6. [Sequence: request dispatch (recv → action table)](#6-sequence-request-dispatch-recv--action-table)
7. [Sequence: create group](#7-sequence-create-group)
8. [Sequence: join group](#8-sequence-join-group)
9. [Sequence: leave / logout / disconnect cleanup](#9-sequence-leave--logout--disconnect-cleanup)
10. [Group lifecycle (ref-count state)](#10-group-lifecycle-ref-count-state)
11. [Notes & observations](#11-notes--observations)
12. [File inventory](#12-file-inventory)

---

## 1. Component overview

```
   serverMain.c
        │  Create / Start / Stop / Destroy
        ▼
   ┌────────────────────────────────────────────────────────┐
   │ ServerManager  (orchestrator + action dispatch)          │
   │   ├─ UserManager   users by-name / by-fd ── User          │
   │   └─ GroupManager  groups by-name ── Group(endpoint,ref)  │
   └────────────────────────────────────────────────────────┘
        │ registers 3 callbacks, replies via SendMessage
        ▼
   ┌────────────────────────────────────────────────────────┐
   │ TcpServerController  (transport façade)                  │
   │   ├─ TcpConnectionAcceptor   accept() loop thread         │
   │   └─ TcpConnectionHandler    select() I/O loop thread     │
   │   TcpConnectionRecord  (fd, ip, port) flows through       │
   └────────────────────────────────────────────────────────┘
        ▲ TCP control plane
   clients
```

`ServerNet` deals only in file descriptors and byte buffers; `ServerMng` deals only
in users, groups, and the chat protocol. The seam between them is **three
function-pointer callbacks** (new-connection, disconnect, message-received) plus
`TcpServerController_SendMessage` for replies.

---

## 2. Class diagram

```mermaid
classDiagram
    direction TB

    class ServerManager {
        -UserManager* m_userManager
        -GroupManager* m_groupManager
        -TcpServerController* m_tcpServerController
        +Create(name, ip, port)$ ServerManager*
        +Destroy(mgr)
        +Start(mgr) ServerResult
        +Stop(mgr) ServerResult
        -CallbackNewConnection(ctx, record)
        -CallbackDisconnect(ctx, record)
        -CallbackRecv(ctx, record, msg, len)
        -ActionRegister/Login/Logout/Exit(...)
        -ActionCreate/Join/LeaveGroup(...)
        -ActionListUsers/ListGroups(...)
        -SendOrLog(record, status, msg, len)
        -LeaveAllGroups(fd)
        -CleanupSession(record)
        -SendGroupEndpoint(record, group)
    }

    class UserManager {
        -HashMap* m_usersByName
        -HashMap* m_usersByFd
        +Create(hash, eq)$ UserManager*
        +AddUser(fd, name, pwd) UserManagerResult
        +Login(fd, name, pwd) UserManagerResult
        +Logout(fd, outName) UserManagerResult
        +AddUserToGroup(fd, group) UserManagerResult
        +RemoveUserFromGroup(fd, group) UserManagerResult
        +GetUserGroupCount(fd, outN) UserManagerResult
        +GetUserGroups(fd, outNames, cap, outN) UserManagerResult
        +IsUserLoggedIn(name, fd, out) UserManagerResult
        +IsUserInGroup(fd, group, out) UserManagerResult
        +FormatAllUsersAndGroups(buf, size) UserManagerResult
    }

    class User {
        -int m_fdConnection
        -char* m_username
        -char* m_password
        -UserState m_state
        -List* m_groups
        +Create(fd, name, pwd)$ User*
        +AddGroup(name) UserResult
        +RemoveGroup(name) UserResult
        +GetState() / SetState()
        +GetUsername() / GetFdConnection()
    }

    class GroupManager {
        -HashMap* m_groups
        +Create(hash, eq)$ GroupManager*
        +AddGroup(name) GroupManagerResult
        +GetGroup(name, out) GroupManagerResult
        +GetGroupEndpoint(name, out) GroupManagerResult
        +RemoveGroupIfEmpty(name) GroupManagerResult
        +IncreaseGroupRefCount(name, out) GroupManagerResult
        +DecreaseGroupRefCount(name, out) GroupManagerResult
        +FormatGroupList(buf, size) GroupManagerResult
        -AllocateGroupEndpoint(groups, out)
    }

    class Group {
        -char* m_name
        -GroupEndpoint m_endpoint
        -size_t m_refCount
        +Create(name, endpoint)$ Group*
        +GetName() / GetEndpoint()
        +GetRefCount() / IncRef() / DecRef()
    }

    class GroupEndpoint {
        +uint32_t m_multicastAddr
        +uint16_t m_port
    }

    class TcpServerController {
        -char* m_name
        -uint32_t m_ip
        -uint16_t m_port
        -ServerState m_state
        -void* m_callbackContext
        -cb m_callbackNewConnection
        -cb m_callbackDisconnect
        -cb m_callbackMessageReceived
        +Create(name, ip, port)$ TcpServerController*
        +SetCallbacks(ctx, onNew, onDisc, onMsg) TcpResult
        +Start() / Stop() / Destroy()
        +ProcessConnection(record) TcpResult
        +NotifyNewConnection(record)
        +ProcessMessage(record, msg, len) TcpResult
        +ProcessDisconnect(record) TcpResult
        +SendMessage(fd, msg, len) TcpResult
    }

    class TcpConnectionAcceptor {
        -TcpServerController* m_tcpCtrl
        -pthread_t m_thread
        -int m_listenFd
        -AcceptorState m_state
        +Start() / Stop()
        -AcceptLoop()
    }

    class TcpConnectionHandler {
        -TcpServerController* m_tcpCtrl
        -pthread_t m_thread
        -HandlerState m_state
        -List* m_connectionsDB
        -fd_set m_activeFdSet
        -int m_wakeupPipe[2]
        +Start() / Stop()
        +AddConnection(record) TcpResult
        -ClientHandlerIOLoop()
    }

    class TcpConnectionRecord {
        +int m_fdConnection
        +char m_ip[INET_ADDRSTRLEN]
        +int m_port
    }

    ServerManager "1" *-- "1" UserManager : owns
    ServerManager "1" *-- "1" GroupManager : owns
    ServerManager "1" *-- "1" TcpServerController : owns
    ServerManager ..> TcpConnectionRecord : in callbacks
    UserManager "1" o-- "*" User : owns (by name & by fd)
    User "1" o-- "*" GroupEndpoint : group names list
    GroupManager "1" o-- "*" Group : owns
    Group "1" *-- "1" GroupEndpoint : has
    TcpServerController "1" *-- "1" TcpConnectionAcceptor : owns
    TcpServerController "1" *-- "1" TcpConnectionHandler : owns
    TcpConnectionAcceptor ..> TcpConnectionRecord : creates
    TcpConnectionHandler "1" o-- "*" TcpConnectionRecord : manages list
    TcpConnectionAcceptor --> TcpServerController : back-ref
    TcpConnectionHandler --> TcpServerController : back-ref
```

**Key design points:**
- `UserManager` indexes users **twice** — `m_usersByName` (login lookup) and
  `m_usersByFd` (per-connection lookup). The same `User*` lives in both maps; the fd
  map is the one used during a live session.
- A `User`'s `m_groups` is a list of **group names** (strings), not `Group*` pointers
  — the server resolves names through `GroupManager` when it needs the endpoint.
- `Group` carries the multicast `GroupEndpoint` plus a **ref count** that drives
  teardown (see §10).

---

## 3. Threading model

```mermaid
flowchart LR
    subgraph Owner["Owner / main thread"]
        M[serverMain: Start/Stop/getchar]
    end
    subgraph Acc["Acceptor thread"]
        A["accept() loop"]
    end
    subgraph Hnd["Handler thread"]
        H["select() loop<br/>(sole owner of conn list + fd_set)"]
    end

    M -. spawns .-> A
    M -. spawns .-> H
    A -- "record* via self-pipe<br/>(atomic pipe write)" --> H
    H -- "fires 3 callbacks on<br/>this thread" --> CB[ServerManager actions]
    M -- "Stop: NULL ptr + shutdown(listenFd)" --> A
    M -- "Stop: NULL ptr through pipe" --> H
```

Three threads, summarized:

| Thread | Runs | Owns / touches |
|--------|------|----------------|
| **Owner/main** | `Create`/`Start`/`Stop`/`Destroy`, blocks on `getchar()` | lifecycle only (serialized by controller mutex) |
| **Acceptor** | `accept()` loop | `m_listenFd`, `_Atomic m_state` |
| **Handler** | `select()` loop | the connection list, `fd_set`, `m_maxFd` — single-owner after `Start` |

**All three `ServerManager` callbacks run on the handler thread**, so every action
handler executes serially on one thread — which is why `UserManager`/`GroupManager`
currently need no locks. The acceptor passes a new `TcpConnectionRecord*` to the
handler through a **self-pipe** (atomic `write` of a pointer); a `NULL` pointer
through that pipe is the stop signal.

---

## 4. Sequence: startup

```mermaid
sequenceDiagram
    autonumber
    participant Main as serverMain
    participant SM as ServerManager
    participant Ctrl as TcpServerController
    participant Acc as Acceptor
    participant Hnd as Handler

    Main->>SM: ServerManager_Create(name, ip, port)
    SM->>SM: UserManager_Create / GroupManager_Create
    SM->>Ctrl: TcpServerController_Create(name, ip, port)
    Ctrl->>Acc: Create (socket/bind/listen)
    Ctrl->>Hnd: Create (pipe for wakeups)
    SM->>Ctrl: SetCallbacks(self, onNew, onDisc, onMsg)
    Main->>SM: ServerManager_Start
    SM->>Ctrl: Start
    Ctrl->>Hnd: Start → spawn select() thread
    Ctrl->>Acc: Start → spawn accept() thread
    Main->>Main: getchar()  (block until operator hits Enter)
```

---

## 5. Sequence: accepting a connection (acceptor → handler)

The cross-thread hand-off that makes new-connection callbacks fire on the *handler*
thread, not the acceptor:

```mermaid
sequenceDiagram
    autonumber
    participant Client
    participant Acc as Acceptor thread
    participant Ctrl as TcpServerController
    participant Hnd as Handler thread
    participant SM as ServerManager

    Client->>Acc: TCP connect
    Acc->>Acc: accept() → fd
    Acc->>Acc: TcpConnectionRecord_Create(fd, ip, port)
    Acc->>Ctrl: ProcessConnection(record)
    Ctrl->>Hnd: AddConnection(record)
    Hnd->>Hnd: write(record*) to wakeup pipe (atomic)
    Note over Hnd: select() returns: pipe readable
    Hnd->>Hnd: ProcessNewConnection: read record*, FD_SET(fd)
    Hnd->>Ctrl: NotifyNewConnection(record)
    Ctrl->>SM: CallbackNewConnection(ctx, record)
    SM->>SM: LOG "New connection from ip:port"
```

If the record pointer read back is `NULL`, that's the stop signal and the loop exits
instead of registering a connection.

---

## 6. Sequence: request dispatch (recv → action table)

```mermaid
sequenceDiagram
    autonumber
    participant Client
    participant Hnd as Handler thread
    participant Ctrl as TcpServerController
    participant SM as ServerManager
    participant Proto as NetworkProtocol

    Client->>Hnd: bytes on socket
    Note over Hnd: select() → FD_ISSET(client fd)
    Hnd->>Hnd: recv(fd, buf)
    alt recv == 0 or error
        Hnd->>Ctrl: ProcessDisconnect(record)
        Ctrl->>SM: CallbackDisconnect → CleanupSession
    else recv > 0
        Hnd->>Ctrl: ProcessMessage(record, buf, n)
        Ctrl->>SM: CallbackRecv(ctx, record, buf, n)
        SM->>Proto: DeserializeChatMessage(buf)
        alt malformed
            SM->>Client: SendOrLog(CHAT_ERR_MALFORMED)
        else ok
            SM->>SM: FindAction(opcode) in s_actions[]
            alt unknown opcode
                SM->>Client: SendOrLog(CHAT_ERR_GENERIC)
            else found
                SM->>SM: action->m_fn(manager, record, &msg)
                SM->>Client: SendOrLog(response)
            end
        end
    end
```

The `s_actions[]` table maps each `OPCODE_*` to a `ServerManager_ActionXxx`
function; dispatch is a linear `FindAction` lookup. Every action replies exactly once
through `SendOrLog`.

---

## 7. Sequence: create group

```mermaid
sequenceDiagram
    autonumber
    participant Client
    participant SM as ServerManager
    participant GM as GroupManager
    participant UM as UserManager

    Client->>SM: OPCODE_CREATE_GROUP ("group\0")
    SM->>GM: AddGroup(name)
    GM->>GM: AllocateGroupEndpoint → 239.0.0.1 : 5000+index
    GM-->>SM: SUCCESS (group created with endpoint)
    SM->>UM: AddUserToGroup(fd, name)
    alt add-to-user fails
        SM->>GM: RemoveGroupIfEmpty(name)  (rollback)
        SM->>Client: SendOrLog(CHAT_ERR_GENERIC)
    else ok
        SM->>GM: IncreaseGroupRefCount(name)
        SM->>GM: GetGroupEndpoint(name) → ep
        SM->>SM: format "ip:port" (network_convert_ip_n_to_p)
        SM->>Client: SendOrLog(CHAT_OK, "239.0.0.1:5000")
    end
```

Note the **rollback**: if the creator can't be added to the just-created group, the
empty group is removed so it never lingers.

---

## 8. Sequence: join group

```mermaid
sequenceDiagram
    autonumber
    participant Client
    participant SM as ServerManager
    participant GM as GroupManager
    participant UM as UserManager

    Client->>SM: OPCODE_JOIN_GROUP ("group\0")
    SM->>GM: GetGroup(name)
    alt not found
        SM->>Client: SendOrLog(CHAT_ERR_GENERIC)
    end
    SM->>UM: IsUserLoggedIn(fd)
    alt not logged in
        SM->>Client: SendOrLog(CHAT_ERR_NOT_LOGGED_IN)
    end
    SM->>UM: AddUserToGroup(fd, name)
    alt ok
        SM->>GM: IncreaseGroupRefCount(name)
        SM->>SM: SendGroupEndpoint → "ip:port"
        SM->>Client: SendOrLog(CHAT_OK, endpoint)
    else fail
        SM->>Client: SendOrLog(CHAT_ERR_GENERIC)
    end
```

---

## 9. Sequence: leave / logout / disconnect cleanup

All three teardown paths funnel through `LeaveAllGroups` (decrement ref counts,
reclaim empty groups). Disconnect and exit additionally tolerate a not-logged-in fd.

```mermaid
sequenceDiagram
    autonumber
    participant Client
    participant SM as ServerManager
    participant UM as UserManager
    participant GM as GroupManager

    alt Leave group (explicit)
        Client->>SM: OPCODE_LEAVE_GROUP ("group\0")
        SM->>GM: GetGroup + checks (logged in, in group)
        SM->>UM: RemoveUserFromGroup(fd, name)
        SM->>GM: DecreaseGroupRefCount(name)
        SM->>GM: RemoveGroupIfEmpty(name)
        SM->>Client: SendOrLog(CHAT_OK)
    else Logout / Exit / Disconnect
        Note over SM: Logout & Exit reply; Disconnect does not (peer gone)
        SM->>UM: IsUserLoggedIn(fd)? (else return)
        SM->>SM: LeaveAllGroups(fd)
        loop each group the user is in
            SM->>GM: DecreaseGroupRefCount(name)
            SM->>GM: RemoveGroupIfEmpty(name)
        end
        SM->>UM: Logout(fd, &username)
    end
```

---

## 10. Group lifecycle (ref-count state)

A group has no explicit "delete" command — it is reclaimed when its ref count
reaches 0.

```mermaid
stateDiagram-v2
    [*] --> Live : CreateGroup\n(AddGroup + IncRef → ref=1)
    Live --> Live : JoinGroup (IncRef)
    Live --> Live : LeaveGroup / Logout (DecRef, ref>0)
    Live --> Removed : DecRef → ref==0\n+ RemoveGroupIfEmpty
    Removed --> [*]

    note right of Live
        Endpoint (239.0.0.x:port) is fixed
        at creation and never changes while Live.
    end note
```

---

## 11. Notes & observations

Carried over from reading the code (see [`server-side.md`](./server-side.md) §14 for
the full list):

- **Single-threaded request processing.** All actions run on the one handler thread,
  so the managers are lock-free *by design*; a future thread pool would require
  adding synchronization to `UserManager`/`GroupManager`.
- **Self-pipe does double duty.** New-connection hand-off and the stop signal share
  one pipe; a `NULL` pointer means "stop."
- **`ChatStatus` value collision.** `CHAT_ERR_NOT_IN_GROUP` and `CHAT_ERR_MALFORMED`
  are both `9` in `NetworkProtocol.h`.
- **Multicast port reuse.** `AllocateGroupEndpoint` derives the port from the live
  group *count*, so a removed group's port can be reassigned later.
- **`TcpConnectionRecord.m_ip`** is now sized `INET_ADDRSTRLEN` (was a hard-coded
  `16`).

---

## 12. File inventory

| File | Role |
|------|------|
| `serverMain.c` | entry point: create/start/stop/destroy `ServerManager` |
| **`ServerMng/`** | |
| `ServerManager.{c,h}` | orchestrator, callbacks, action table, send helpers |
| `UserManager.{c,h}` | user registry (by-name + by-fd), login/logout, membership |
| `User.{c,h}` | a single user (state, fd, password, group-name list) |
| `GroupManager.{c,h}` | group registry, endpoint allocation, ref counting |
| `Group.{c,h}` | a single group (name, endpoint, ref count) |
| **`ServerNet/`** | |
| `TcpServerController.{c,h}` | transport façade; owns acceptor + handler; `SendMessage` |
| `TcpConnectionAcceptor.{c,h}` | `accept()` loop thread |
| `TcpConnectionHandler.{c,h}` | `select()` I/O loop thread; owns the connection list |
| `TcpConnectionRecord.{c,h}` | per-connection value object (fd, ip, port) |
| `class_diagram.mmd` | original Mermaid class diagram of the `ServerNet` layer |

---

*Companion documents:* [`server-side.md`](./server-side.md) (prose architecture),
[`client-side.md`](./client-side.md) (client UML), and
[`multicast-chat.md`](./multicast-chat.md) (UDP data plane).
