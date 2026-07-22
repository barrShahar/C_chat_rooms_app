# C Chat Rooms — Multi-Client Chat System in C

A multi-user chat system written in **pure C11** with **no external
dependencies** beyond the POSIX/Linux system libraries. It pairs a
multithreaded TCP control server with **UDP-multicast group messaging**, a
hand-written data-structures library, and a compact binary wire protocol.

The project is built as an exercise in **systems / embedded-style C**: bounded
fixed-size buffers, explicit manual memory management, byte-level protocol
framing, and concurrency without a heavyweight runtime.

---

## Architecture at a glance

The design separates a **control plane** (reliable, connection-oriented) from a
**data plane** (fan-out group messaging):

- **Control plane — TCP.** Register, log in, create/join/leave groups, and list
  users or groups. The server assigns every group a **UDP multicast endpoint**
  from the administratively-scoped `239.0.0.0/8` block and hands it back to the
  client.
- **Data plane — UDP multicast.** Once a client knows a group's endpoint it
  joins that multicast channel; chat messages fan out to all members without
  the server relaying each one.

```
                    TCP control channel (register / login / groups)
   ┌──────────┐  ───────────────────────────────────────────►  ┌──────────────┐
   │  Client  │                                                 │    Server    │
   │ (single- │  ◄───────────────────────────────────────────  │ (multithread)│
   │ threaded)│        assigns "239.x.x.x:port" per group       └──────────────┘
   └────┬─────┘
        │ spawns per-group terminal windows (POSIX mq for PIDs)
        ▼
   ┌──────────────┐        UDP multicast group channel        ┌──────────────┐
   │ chat_sender  │  ─────────────────────────────────────►   │ chat_receiver│
   │  (per group) │        239.x.x.x:port  (all members)      │  (per group) │
   └──────────────┘                                           └──────────────┘
```

### Module layout

| Directory       | Responsibility                                                        | Artifact                  |
|-----------------|-----------------------------------------------------------------------|---------------------------|
| `ServerNet/`    | TCP transport layer — acceptor + I/O worker threads, `select()` loop  | `libtcpserver.a`          |
| `ServerMng/`    | Business logic — `User`, `Group`, and their managers, `ServerManager` | linked into `out.serverMain` |
| `ClientNet/`    | Client-side TCP controller (protocol encode/decode)                   | `libclientcontroller.a`   |
| `Client/`       | Interactive terminal app — menu, state machine, group windows         | `libclientapp.a`          |
| `ChatWindows/`  | Standalone `chat_sender` / `chat_receiver` multicast programs         | `out.chat_sender/receiver`|
| `utils/`        | Logger, socket helpers, enum-generation macros                        | —                         |
| `db/`           | Hand-written data-structures library (see below)                      | `libDataStructures.a`     |
| `NetworkProtocol.h` | Binary wire-protocol definition (opcodes, status codes, framing)  | —                         |
| `config.h`      | All compile-time tunables (ports, buffer sizes, capacities)           | —                         |

---

## Highlights (the systems-programming parts)

- **Lock-free connection handling.** The TCP server runs an acceptor thread and
  an I/O worker thread. New connections are handed between them with the
  **self-pipe trick** (a `≤ PIPE_BUF` pointer write is atomic per POSIX), so the
  hot path takes **no locks**. State is coordinated with C11 `_Atomic` flags and
  a single mutex that guards only lifecycle calls (start/stop/destroy).
  Full write-up with diagrams: [`docs/thread-safety.md`](docs/thread-safety.md).

- **Compact binary wire protocol.** Every message is `[opcode:1][length:2 LE]
  [payload:length]`, with payloads bounded to 256 bytes — the same
  length-prefixed framing discipline used for UART / serial and packet-based
  device links. Defined in [`NetworkProtocol.h`](NetworkProtocol.h).

- **UDP multicast fan-out.** Groups map to multicast endpoints so message
  delivery scales to all members without per-client relaying on the server.

- **Multi-process client UI.** The client spawns dedicated `chat_sender` /
  `chat_receiver` terminal windows per group and coordinates them over a
  **POSIX message queue** (`mq_*`) that reports child PIDs back to the parent.

- **Hand-written data structures.** No reliance on a rich runtime: `db/` ships a
  hash map, doubly-linked list, heap, queue, stack, vector, and binary tree,
  all built into `libDataStructures.a`.

- **Bounded, config-driven resource use.** Buffer sizes, hash-map bucket counts,
  vector capacities, and multicast ranges are all fixed compile-time constants
  in [`config.h`](config.h) — no unbounded growth in the steady state.

---

## Building

Requires `gcc`, `make`, and a Linux/POSIX environment (uses `pthread`, `librt`,
and BSD sockets). The interactive client also uses `gnome-terminal` to open
per-group chat windows.

```sh
make            # builds server, client, and the chat-window helpers
make clean      # removes all build artifacts (recurses into sub-modules)
```

Outputs land in `build/`:

| Binary                   | Role                                       |
|--------------------------|--------------------------------------------|
| `build/out.serverMain`   | The chat server                            |
| `build/out.client`       | The interactive client                     |
| `build/out.chat_sender`  | Per-group multicast sender (spawned by client) |
| `build/out.chat_receiver`| Per-group multicast receiver (spawned by client) |

---

## Running

Defaults (in [`config.h`](config.h)): server binds `127.0.0.1:8080`.

```sh
# Terminal 1 — start the server (press Enter to shut it down cleanly)
./build/out.serverMain

# Terminal 2+ — start one or more clients
./build/out.client
```

From the client menu you can register/log in, create or join groups, and list
users and groups. Joining a group opens dedicated send/receive terminal windows
bound to that group's multicast channel.

---

## Testing

Each network module ships its own tests:

```sh
make -C ServerNet        # builds transport-layer test apps + multi-client harness
make -C ClientNet        # builds ClientNet unit tests
make -C Client           # builds client/menu tests
make -C tests            # protocol-level tests
```

See each module's `Makefile`, `tests/`, and `unittests/` for the available
targets and smoke scripts.

---

## Configuration

All tunables live in [`config.h`](config.h): server IP/port, receive buffer
size, hash-map bucket counts, per-user vector capacities, the multicast address
range and port base, and the IPC queue name and timeouts. Change a constant and
rebuild — no runtime configuration files.
