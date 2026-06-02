#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include "TcpConnectionHandler.h"
#include "TcpServerController.h"
#include "../db/gen_dlist.h"
#include "logger.h"
#include "config.h"

/* ----------------------------------------------------------------------------
 * Threading model
 * ----------------------------------------------------------------------------
 * Threads that touch a TcpConnectionHandler instance:
 *
 *   1. Owner thread (main): Create / Start / Stop / Destroy. These are
 *      serialized by the parent controller's mutex, so no per-handler lock
 *      is needed for lifecycle management.
 *
 *   2. Acceptor thread: calls TcpConnectionHandler_AddConnection(), which
 *      pushes a TcpConnectionRecord pointer through the self-pipe.
 *
 *   3. Handler worker thread (spawned in Start): runs ClientHandlerIOLoop,
 *      blocks in select(), services client fds, and is the SOLE owner of
 *      the connections list and the fd_set after Start.
 *
 * Cross-thread communication:
 *   - m_state:      _Atomic. Written by owner (Start/Stop), read by worker.
 *   - m_wakeupPipe: the self-pipe trick. A pipe(2) write of sizeof(void*)
 *                   bytes is atomic per POSIX, so the acceptor can shove a
 *                   record pointer through it without any lock. The worker
 *                   sees pipe[0] become readable inside its select() and
 *                   reads the pointer back out. A NULL pointer through the
 *                   pipe doubles as the "wake up, you've been told to stop"
 *                   signal from Stop().
 *
 * Single-owner invariants (held by the worker thread once Start returns):
 *   - m_connectionsDB:    the list is constructed in Create, then mutated
 *                         exclusively from the worker (ProcessNewConnection
 *                         appends, ProcessClientData removes). Destroy is
 *                         only called AFTER pthread_join, so no concurrent
 *                         access is possible.
 *   - m_activeFdSet,
 *     m_activeFdSetCopy,
 *     m_maxFd:            same story. Only the worker reads or writes them
 *                         while the thread is alive.
 * ---------------------------------------------------------------------------- */


typedef enum {
    HANDLER_STATE_STOPPED,
    HANDLER_STATE_RUNNING
} HandlerState;

struct TcpConnectionHandler
{
    TcpServerController* m_tcpCtrl;
    pthread_t m_thread;
    /* _Atomic for the same reason as the acceptor's m_state: read every
     * iteration of the IO loop, written by Stop() from another thread. */
    _Atomic HandlerState m_state;
    List* m_connectionsDB;          /* worker-thread-only after Start */

    /* select() book-keeping. All of these are worker-thread-private. */
    int m_maxFd;
    fd_set m_activeFdSet;
    fd_set m_activeFdSetCopy;
    size_t m_numberOfClients;

    /* Self-pipe used both for new-connection notification (acceptor -> worker)
     * and for the stop signal (owner -> worker). A NULL pointer pushed
     * through this pipe means "stop". */
    int m_wakeupPipe[2];
};

// Macros
#define PIPE_READ  0
#define PIPE_WRITE 1
#define CHECK_WRITE_SIZE(actual_bytes, expected_bytes) \
    (((actual_bytes) == (ssize_t)(expected_bytes)) ? TCP_RESULT_SUCCESS : TCP_RESULT_SOCKET_ERROR)
#define UPDATE_MAX_FD(current_max, new_fd) \
    do { if ((new_fd) > (current_max)) { (current_max) = (new_fd); } } while(0)

// statics
static void* ClientHandlerIOLoop(void* a_handler);
static void DestroyRecord(void* a_item);
static int WaitForActivity(TcpConnectionHandler* handler);
static void RegisterNewConnectionsFromAcceptor(TcpConnectionHandler* handler, int* nReady);
static void ServiceExistingConnections(TcpConnectionHandler* handler, int* nReady);

// API 
TcpConnectionHandler* 
TcpConnectionHandler_Create(TcpServerController* a_tcpCtrl)
{
    TcpConnectionHandler* handler = (TcpConnectionHandler*)malloc(sizeof(TcpConnectionHandler));
    List* list = ListCreate();
    if (handler == NULL || list == NULL)
    {
        LOG_ERROR("allocation failed: %s", strerror(errno));
        ListDestroy(&list, NULL);
        free(handler);
        return NULL;
    }
    if (pipe(handler->m_wakeupPipe) < 0)
    {
        LOG_ERROR("pipe creation failed: %s", strerror(errno));
        ListDestroy(&list, NULL);
        free(handler);
        return NULL;
    }

    handler->m_connectionsDB = list;
    handler->m_tcpCtrl = a_tcpCtrl;
    /* No worker thread exists yet, so we are alone with the struct. Still
     * use atomic_store for consistency with later reads. */
    atomic_store(&handler->m_state, HANDLER_STATE_STOPPED);
    FD_ZERO(&handler->m_activeFdSet);
    FD_ZERO(&handler->m_activeFdSetCopy);
    FD_SET(handler->m_wakeupPipe[PIPE_READ], &handler->m_activeFdSet);
    handler->m_maxFd = handler->m_wakeupPipe[PIPE_READ];
    return handler;
}

void 
TcpConnectionHandler_Destroy(TcpConnectionHandler** a_handler)
{
    if (a_handler == NULL || *a_handler == NULL)
    {
        return;
    }
    /* Stop joins the worker thread first. After this returns, we are
     * single-threaded again and can safely free everything. */
    TcpConnectionHandler_Stop(*a_handler);
    TcpConnectionHandler* handler = *a_handler;
    ListDestroy(&handler->m_connectionsDB, DestroyRecord);
    close(handler->m_wakeupPipe[PIPE_READ]);
    close(handler->m_wakeupPipe[PIPE_WRITE]);
    free(handler);
    *a_handler = NULL;
}

TcpResult 
TcpConnectionHandler_Start(TcpConnectionHandler* a_handler)
{
    if (a_handler == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }

    if (atomic_load(&a_handler->m_state) == HANDLER_STATE_RUNNING)
    {
        return TCP_RESULT_SUCCESS;
    }

    /* Publish RUNNING BEFORE pthread_create (mirrors the acceptor). The
     * worker's loop predicate is `m_state == RUNNING`; if we stored RUNNING
     * only after pthread_create, a worker that raced ahead of the store would
     * read STOPPED, never enter the loop, and exit immediately — leaving a
     * dead handler that silently never services any connection. Storing
     * first guarantees the worker sees RUNNING on its first read. */
    atomic_store(&a_handler->m_state, HANDLER_STATE_RUNNING);
    int result = pthread_create(&a_handler->m_thread, NULL, ClientHandlerIOLoop, a_handler);
    if (result != 0)
    {
        /* Roll back so a later Start can retry and a later Stop won't try to
         * join a thread that never came into existence. */
        atomic_store(&a_handler->m_state, HANDLER_STATE_STOPPED);
        LOG_ERROR("pthread_create failed: %s", strerror(result));
        return TCP_RESULT_THREAD_CREATION_FAILED;
    }

    return TCP_RESULT_SUCCESS;
}



void
TcpConnectionHandler_Stop(TcpConnectionHandler* a_handler)
{
    if (a_handler == NULL)
    {
        return;
    }

    /* atomic_exchange returns the OLD value. If it wasn't RUNNING then either
     * we never started or someone else already stopped us; either way, the
     * thread isn't joinable and we must NOT call pthread_join. */
    if (atomic_exchange(&a_handler->m_state, HANDLER_STATE_STOPPED) != HANDLER_STATE_RUNNING)
    {
        return;
    }

    /* Wake the worker. It's blocked in select() waiting for pipe activity.
     * Writing a NULL pointer is interpreted by the worker as "stop signal".
     * The state flip above ensures that even if the worker reaches the
     * loop condition before processing this pointer, it will exit cleanly. */
    TcpConnectionRecord* stop = NULL;
    write(a_handler->m_wakeupPipe[PIPE_WRITE], &stop, sizeof(stop));
    pthread_join(a_handler->m_thread, NULL);
    LOG_INFO("Handler thread stopped");
}

/* Called from the ACCEPTOR thread (and only the acceptor thread in this
 * design). We must not touch the connections list, fd_set, or m_maxFd
 * here — those are exclusively owned by the worker. Instead we hand the
 * record off via the self-pipe, and the worker pulls it out and registers
 * it on its own thread. POSIX guarantees that a write() of <= PIPE_BUF
 * bytes is atomic, so even if multiple acceptors wrote concurrently
 * (we only have one today) their pointers would not be interleaved. */
TcpResult
TcpConnectionHandler_AddConnection(TcpConnectionHandler* a_handler, TcpConnectionRecord* a_record)
{
    if (a_handler == NULL || a_record == NULL) 
    {
        return TCP_RESULT_NULL_PTR;
    }
    ssize_t written = write(a_handler->m_wakeupPipe[PIPE_WRITE], &a_record, sizeof(a_record));
    return CHECK_WRITE_SIZE(written, sizeof(a_record));
}

/* Worker-thread-only. Pulls a record pointer out of the self-pipe and,
 * unless it's the NULL stop signal, registers the new fd with select().
 * Mutating m_connectionsDB / m_activeFdSet / m_maxFd here is safe because
 * we are the sole writer; the acceptor never touches these fields. */
static void 
ProcessNewConnection(TcpConnectionHandler* handler)
{
    TcpConnectionRecord* record;
    if (read(handler->m_wakeupPipe[PIPE_READ], &record, sizeof(record)) == sizeof(record))
    {
        /* NULL pointer = Stop() told us to wake up and exit. The loop
         * predicate (atomic_load(m_state)) will catch the state change
         * on the next iteration; we just return here. */
        if (record == NULL) return;
        // fd size gaurd for new connections
        if (record->m_fdConnection >= FD_SETSIZE)
        {
            LOG_ERROR("invalid fd=%d for new connection", record->m_fdConnection);
            TcpServerController_ProcessDisconnect(handler->m_tcpCtrl, record);
            return;
        }
        ListPushTail(handler->m_connectionsDB, record);
        FD_SET(record->m_fdConnection, &handler->m_activeFdSet);
        UPDATE_MAX_FD(handler->m_maxFd, record->m_fdConnection);
        LOG_DEBUG("connection fd=%d registered, maxFd=%d", record->m_fdConnection, handler->m_maxFd);
        /* Fire the new-connection callback here, on the worker thread, now
         * that the record is registered and owned by us — the acceptor must
         * not touch it after the pipe handoff (use-after-free otherwise). */
        TcpServerController_NotifyNewConnection(handler->m_tcpCtrl, record);
    }
}

static void 
ProcessClientData(TcpConnectionHandler* handler, TcpConnectionRecord* record, ListItr currentItr) 
{
    char buf[CONF_RECV_BUF_SIZE];
    ssize_t n = recv(record->m_fdConnection, buf, sizeof(buf), 0);

    if (n <= 0)
    {
        if (n == 0) {
            LOG_DEBUG("client fd=%d disconnected", record->m_fdConnection);
        } else {
            LOG_ERROR("recv error on fd=%d: %s", record->m_fdConnection, strerror(errno));
        }
        FD_CLR(record->m_fdConnection, &handler->m_activeFdSet);
        ListItrRemove(currentItr);
        TcpServerController_ProcessDisconnect(handler->m_tcpCtrl, record);
    } 
    else 
    {
        // Data received successfully
        TcpServerController_ProcessMessage(handler->m_tcpCtrl, record, buf, (size_t)n);
    }
}

/* Worker-thread entry point. This is the ONLY function (along with its
 * static callees) that mutates the connection list and the fd_set after
 * Start() returns. Everything in here runs on a single thread, so the
 * data-structure operations are race-free by ownership, not by locking. */
static void* 
ClientHandlerIOLoop(void* a_handler)
{
    TcpConnectionHandler* handler = (TcpConnectionHandler*)a_handler;

    /* The atomic_load here is the cross-thread observation point for Stop().
     * Without _Atomic the compiler could legally cache the value in a
     * register across iterations and turn this into an infinite loop. */
    while (atomic_load(&handler->m_state) == HANDLER_STATE_RUNNING)
    {
        int nReady = WaitForActivity(handler);
        
        if (nReady < 0) break;     // Break the loop on fatal errors
        if (nReady == 0) continue; // Loop again on timeout or interrupt

        // 1. Handle main thread tapping us on the shoulder
        RegisterNewConnectionsFromAcceptor(handler, &nReady);
        
        // 2. Handle actual client data (if there are still unhandled events)
        if (nReady > 0) 
        {
            ServiceExistingConnections(handler, &nReady);
        }
    }

    return NULL;
}

static void 
DestroyRecord(void* a_item)
{
    TcpConnectionRecord* record = (TcpConnectionRecord*)a_item;
    close(record->m_fdConnection);
    free(record);
}

static int 
WaitForActivity(TcpConnectionHandler* handler) 
{
    handler->m_activeFdSetCopy = handler->m_activeFdSet;    
    int nReady = select(handler->m_maxFd + 1, &handler->m_activeFdSetCopy, NULL, NULL, 0);

    if (nReady < 0 && errno != EINTR)
    {
        LOG_ERROR("select failed: %s", strerror(errno));
        return -1;
    }
    
    // If nReady < 0 here, it was EINTR (interrupted), so we treat it as 0 to retry.
    return (nReady < 0) ? 0 : nReady; 
}

static void 
RegisterNewConnectionsFromAcceptor(TcpConnectionHandler* handler, int* nReady) 
{
    if (FD_ISSET(handler->m_wakeupPipe[0], &handler->m_activeFdSetCopy)) 
    {
        ProcessNewConnection(handler);
        (*nReady)--; // Decrement because we handled one event
    }
}

static void 
ServiceExistingConnections(TcpConnectionHandler* handler, int* nReady) 
{
    ListItr itr = ListItrBegin(handler->m_connectionsDB);
    ListItr end = ListItrEnd(handler->m_connectionsDB);

    // Stop looping early if nReady hits 0 (optimization)
    while (itr != end && *nReady > 0) 
    {
        TcpConnectionRecord* record = (TcpConnectionRecord*)ListItrGet(itr);
        ListItr next = ListItrNext(itr);

        if (FD_ISSET(record->m_fdConnection, &handler->m_activeFdSetCopy)) 
        {
            (*nReady)--;
            ProcessClientData(handler, record, itr);
        }
        itr = next;
    }
}