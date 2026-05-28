#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include "TcpServerController.h"
#include "TcpConnectionAcceptor.h"
#include "network_utils.h"
#include "TcpConnectionHandler.h"
#include "TcpConnectionRecord.h"
#include "logger.h"

/* ----------------------------------------------------------------------------
 * Threading model
 * ----------------------------------------------------------------------------
 * The controller is the synchronization root for the whole server.
 *
 * Threads that touch a TcpServerController instance:
 *
 *   1. Owner thread: calls Create / Start / Stop / Destroy / SetCallbacks /
 *      Get*. The public lifecycle/configuration API is serialized by
 *      m_lock so it is safe to call these from any single thread, but
 *      callers must NOT call them concurrently from multiple threads
 *      (the contract documented in the header).
 *
 *   2. Acceptor thread: calls ProcessConnection, which only hands the record
 *      to the handler via the self-pipe. It does NOT invoke any callback.
 *
 *   3. Handler worker thread: calls NotifyNewConnection (after registering
 *      the record), ProcessMessage, and ProcessDisconnect, which invoke the
 *      corresponding callbacks. All three callbacks thus run on this one
 *      thread, so the record's lifetime never crosses a thread boundary.
 *
 * Shared mutable state:
 *   - m_state: _Atomic ServerState. Tracks RUNNING/STOPPED. Read by all
 *              three threads (workers don't read it directly today, but
 *              the owner reads it inside Start/Stop). Written only under
 *              m_lock by the owner.
 *   - m_lock:  serializes the lifecycle/configuration calls. The hot path
 *              (ProcessConnection/Message/Disconnect) does NOT take this
 *              lock — see the "callback pointers" note below.
 *
 * Effectively-immutable-after-Create state (no synchronization needed):
 *   - m_name, m_ip, m_port, m_connectionAcceptor, m_connectionHandler
 *
 * Callback pointers (m_callbackNewConnection / Disconnect / MessageReceived):
 *   These are read on the hot path by worker threads, with NO lock. That
 *   is only safe because we require SetCallbacks to be called BEFORE
 *   Start (enforced: SetCallbacks returns INVALID_ARGUMENT if state is
 *   RUNNING). The pthread_create inside Start acts as a happens-before
 *   edge, so worker threads observe the final callback values. After
 *   Stop joins the workers (also a happens-before edge), SetCallbacks
 *   may be called again before another Start.
 *
 * Lock ordering:
 *   m_lock is the only lock in this module. It is NEVER held while
 *   calling into the acceptor or handler subsystems EXCEPT in Start
 *   (which calls *_Start) and Destroy/Stop (which call *_Stop and
 *   *_Destroy). Those subordinate calls do not call back into the
 *   controller, so no inversion is possible. Critically, the worker-
 *   thread paths (ProcessConnection/Message/Disconnect) never acquire
 *   m_lock, so Stop holding m_lock while calling pthread_join in the
 *   subordinate Stop functions cannot deadlock.
 * ---------------------------------------------------------------------------- */

static char* CopyString(const char* a_string);

/* StopUnlocked is the internal stop primitive. It does NOT take m_lock and
 * MUST be called with m_lock already held. Used by both Stop() (which
 * locks first) and Destroy() (which locks once for the whole tear-down). */
static void StopUnlocked(TcpServerController* a_ctrl);

const char* TcpResult_ToString(TcpResult a_result)
{
    switch (a_result)
    {
    case TCP_RESULT_SUCCESS:                 return "success";
    case TCP_RESULT_NULL_PTR:                return "null pointer";
    case TCP_RESULT_ALLOCATION_FAILED:       return "allocation failed";
    case TCP_RESULT_SOCKET_ERROR:            return "socket error";
    case TCP_RESULT_BIND_ERROR:              return "bind error";
    case TCP_RESULT_LISTEN_ERROR:            return "listen error";
    case TCP_RESULT_ACCEPT_ERROR:            return "accept error";
    case TCP_RESULT_CONNECTION_CLOSED:       return "connection closed";
    case TCP_RESULT_THREAD_CREATION_FAILED:  return "thread creation failed";
    default:                                 return "unknown";
    }
}

typedef enum {
    SERVER_STATE_STOPPED,
    SERVER_STATE_RUNNING
} ServerState;

struct TcpServerController
{
    /* Immutable after Create: safe to read from any thread without sync. */
    char* m_name;
    TcpConnectionAcceptor* m_connectionAcceptor;
    TcpConnectionHandler* m_connectionHandler;
    uint32_t m_ip;
    uint16_t m_port;

    /* _Atomic so the lifecycle calls can do fast lockless reads
     * (e.g. an "already running?" check) and so any future read from a
     * worker thread is well-defined. All WRITES happen under m_lock. */
    _Atomic ServerState m_state;

    /* Guards the lifecycle/configuration API:
     *   Start / Stop / Destroy / SetCallbacks
     * Does NOT guard the hot path (ProcessConnection / Message / Disconnect)
     * — those rely on the "callbacks are immutable while RUNNING" invariant. */
    pthread_mutex_t m_lock;

    /* Callbacks + context: only mutated by SetCallbacks, which requires state==STOPPED
     * (enforced under m_lock). Read by worker threads on the hot path with
     * no lock. The happens-before edges through pthread_create (in Start)
     * and pthread_join (in Stop) make this safe. */
    void* m_callbackContext;
    void (*m_callbackNewConnection)(void* a_context, const TcpConnectionRecord* a_record);
    void (*m_callbackDisconnect)(void* a_context, const TcpConnectionRecord* a_record);
    void (*m_callbackMessageReceived)(void* a_context, const TcpConnectionRecord* a_record, const char* a_message, size_t a_length);
};

TcpServerController* 
TcpServerController_Create(const char* a_name, const char* a_ip, const uint16_t a_port)
{
    TcpServerController* controller = (TcpServerController*)malloc(sizeof(TcpServerController));
    if (controller == NULL)
    {
        return NULL;
    }
    controller->m_name = CopyString(a_name);
    if (controller->m_name == NULL)
    {
        free(controller);
        return NULL;
    }
    controller->m_ip = network_convert_ip_p_to_n(a_ip);
    controller->m_port = a_port;
    /* malloc does not zero: initialize callbacks so the hot-path NULL checks
     * are well-defined even if the caller never calls SetCallbacks. */
    controller->m_callbackContext = NULL;
    controller->m_callbackNewConnection = NULL;
    controller->m_callbackDisconnect = NULL;
    controller->m_callbackMessageReceived = NULL;
    /* Initialize state BEFORE the mutex so that if mutex init fails we have
     * not yet committed to any threading-related resource we'd need to
     * tear down. Atomic store on a single-threaded controller is trivially
     * fine but kept for symmetry with the rest of the code. */
    atomic_store(&controller->m_state, SERVER_STATE_STOPPED);
    if (pthread_mutex_init(&controller->m_lock, NULL) != 0)
    {
        free(controller->m_name);
        free(controller);
        return NULL;
    }

    controller->m_connectionAcceptor = TcpConnectionAcceptor_Create(controller);
    controller->m_connectionHandler = TcpConnectionHandler_Create(controller);

    if (controller->m_connectionAcceptor == NULL || controller->m_connectionHandler == NULL)
    {
        TcpServerController_Destroy(&controller);
        return NULL;
    }

    LOG_INFO("server '%s' created on %s:%d", a_name, a_ip, a_port);
    return controller;
}

TcpResult
TcpServerController_SendMessage(TcpServerController* a_controller,
    const int a_fd,
    const char* a_message, 
    size_t a_length)
{
    if (a_controller == NULL || a_message == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }
    if (a_length == 0 || a_fd < 0)
    {
        return TCP_RESULT_INVALID_ARGUMENT;
    }

    if (send(a_fd, a_message, a_length, 0) < 0)
    {
        return TCP_RESULT_SOCKET_ERROR;
    }

    return TCP_RESULT_SUCCESS;
}

TcpResult 
TcpServerController_Display(TcpServerController* a_ctrl)
{
    if (a_ctrl == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }

    LOG_DEBUG("Display: name=%s ip=%u port=%d state=%d", a_ctrl->m_name, a_ctrl->m_ip, a_ctrl->m_port,
        (int)atomic_load(&a_ctrl->m_state));
    return TCP_RESULT_SUCCESS;
}

void
TcpServerController_Destroy(TcpServerController** a_ctrl)
{
    if (a_ctrl == NULL || *a_ctrl == NULL)
    {
        return;
    }
    LOG_DEBUG("Destroying server '%s'", (*a_ctrl)->m_name);
    TcpServerController* controller = *a_ctrl;

    /* Hold m_lock across the whole tear-down so that no concurrent
     * Start/Stop/SetCallbacks can sneak in while we are destroying
     * subobjects. We use StopUnlocked (not Stop) here because Stop
     * would recursively try to lock m_lock and we'd self-deadlock. */
    LOG_DEBUG("Locking mutex for server '%s'", controller->m_name);
    pthread_mutex_lock(&controller->m_lock);
    StopUnlocked(controller);
    TcpConnectionAcceptor_Destroy(&controller->m_connectionAcceptor);
    TcpConnectionHandler_Destroy(&controller->m_connectionHandler);
    pthread_mutex_unlock(&controller->m_lock);

    /* Safe to destroy the mutex now: workers are joined (via StopUnlocked
     * -> *_Stop -> pthread_join) and no other thread can be racing us. */
    LOG_DEBUG("Destroying mutex for server '%s'", controller->m_name);
    pthread_mutex_destroy(&controller->m_lock);
    LOG_DEBUG("Freeing name for server '%s'", controller->m_name);
    free(controller->m_name);
    LOG_DEBUG("Freeing controller for server\n");
    free(controller);
    *a_ctrl = NULL;
    LOG_DEBUG("Server destroyed");
}

TcpResult TcpServerController_Start(TcpServerController* a_ctrl)
{
    if (a_ctrl == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }

    /* Lock for the whole transition so that two threads racing on Start
     * cannot both pass the "already running?" check and spawn workers twice. */
    pthread_mutex_lock(&a_ctrl->m_lock);

    if (atomic_load(&a_ctrl->m_state) == SERVER_STATE_RUNNING)
    {
        LOG_WARN("server '%s' is already running", a_ctrl->m_name);
        pthread_mutex_unlock(&a_ctrl->m_lock);
        return TCP_RESULT_SUCCESS;
    }

    /* If either subsystem fails to start, roll back the one that may have
     * partially started. *_Stop is idempotent so calling it on a never-
     * started subsystem is harmless (it'll see STOPPED and return).
     * pthread_create inside *_Start is the happens-before edge that
     * publishes the callback pointers to the new worker threads. */
    if (TcpConnectionAcceptor_Start(a_ctrl->m_connectionAcceptor) != TCP_RESULT_SUCCESS
        || TcpConnectionHandler_Start(a_ctrl->m_connectionHandler) != TCP_RESULT_SUCCESS)
    {
        TcpConnectionAcceptor_Stop(a_ctrl->m_connectionAcceptor);
        TcpConnectionHandler_Stop(a_ctrl->m_connectionHandler);
        pthread_mutex_unlock(&a_ctrl->m_lock);
        return TCP_RESULT_THREAD_CREATION_FAILED;
    }

    atomic_store(&a_ctrl->m_state, SERVER_STATE_RUNNING);
    pthread_mutex_unlock(&a_ctrl->m_lock);
    return TCP_RESULT_SUCCESS;
}

/* MUST be called with m_lock held. Tears down the worker threads in both
 * subsystems and flips the state to STOPPED. Both *_Stop calls are
 * idempotent and join their respective worker threads internally, so
 * after this returns no worker is running on behalf of this controller. */
static void
StopUnlocked(TcpServerController* a_ctrl)
{
    if (atomic_load(&a_ctrl->m_state) == SERVER_STATE_STOPPED)
    {
        return;
    }

    /* Order matters slightly: stop the acceptor first so no new connections
     * arrive at the handler mid-shutdown. */
    TcpConnectionAcceptor_Stop(a_ctrl->m_connectionAcceptor);
    TcpConnectionHandler_Stop(a_ctrl->m_connectionHandler);
    atomic_store(&a_ctrl->m_state, SERVER_STATE_STOPPED);
    LOG_INFO("server '%s' stopped", a_ctrl->m_name);
}

void
TcpServerController_Stop(TcpServerController* a_ctrl)
{
    if (a_ctrl == NULL)
    {
        return;
    }

    /* Lock so that two concurrent Stop()s, or a Stop() racing a Start(),
     * are serialized. The underlying *_Stop functions are themselves
     * idempotent (atomic_exchange-based), but the controller's state
     * flag transition needs the surrounding lock to stay consistent. */
    pthread_mutex_lock(&a_ctrl->m_lock);

    if (atomic_load(&a_ctrl->m_state) == SERVER_STATE_STOPPED)
    {
        LOG_WARN("server '%s' is already stopped", a_ctrl->m_name);
        pthread_mutex_unlock(&a_ctrl->m_lock);
        return;
    }

    StopUnlocked(a_ctrl);
    pthread_mutex_unlock(&a_ctrl->m_lock);
}

/* Called on the ACCEPTOR thread. Its ONLY job is to hand the record off to
 * the handler via the self-pipe. It must NOT touch a_record after a
 * successful AddConnection: the moment the pointer is in the pipe the record
 * is owned by the handler thread, which may register, service, and free it
 * concurrently — so dereferencing it here (even merely to log it or to fire
 * the new-connection callback) is a use-after-free. The new-connection
 * callback therefore fires on the handler thread instead, from
 * ProcessNewConnection -> TcpServerController_NotifyNewConnection. */
TcpResult
TcpServerController_ProcessConnection(TcpServerController* a_controller,
    TcpConnectionRecord* a_record)
{
    if (a_controller == NULL || a_record == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }

    TcpResult resultHandlerDb =
        TcpConnectionHandler_AddConnection(a_controller->m_connectionHandler, a_record);

    if (resultHandlerDb != TCP_RESULT_SUCCESS)
    {
        LOG_ERROR("failed to add connection to handler: %s", TcpResult_ToString(resultHandlerDb));
        return resultHandlerDb; // The acceptor must destroy the record
    }

    return TCP_RESULT_SUCCESS;
}

/* Called on the HANDLER worker thread, after the handler has registered the
 * record and while it still owns it (so the record is guaranteed alive).
 * Firing the new-connection callback here — rather than on the acceptor
 * thread — keeps the record's whole lifetime on one thread and delivers all
 * three callbacks (new / message / disconnect) in order from that same
 * thread. Lock-free for the same reason as the other Process* functions:
 * the callback pointer is frozen while the server is RUNNING. */
void
TcpServerController_NotifyNewConnection(TcpServerController* a_controller,
    const TcpConnectionRecord* a_record)
{
    if (a_controller == NULL || a_record == NULL)
    {
        return;
    }

    LOG_DEBUG("new connection from %s:%d (fd=%d)",
        a_record->m_ip, a_record->m_port, a_record->m_fdConnection);

    if (a_controller->m_callbackNewConnection)
    {
        a_controller->m_callbackNewConnection(a_controller->m_callbackContext, a_record);
    }
}

/* Called on the HANDLER worker thread (hot path). Lock-free for the same
 * reason as ProcessConnection: the callback pointer is immutable while
 * the server is RUNNING. */
TcpResult 
TcpServerController_ProcessMessage(
    TcpServerController* a_controller, 
    const TcpConnectionRecord* a_record, 
    const char* a_message, size_t a_length)
{
    if (a_controller == NULL || a_record == NULL || a_message == NULL) return TCP_RESULT_NULL_PTR;
    LOG_DEBUG("Processing message from %s:%d (fd=%d)", a_record->m_ip, a_record->m_port, a_record->m_fdConnection);
    if (a_length == 0) return TCP_RESULT_INVALID_ARGUMENT;

    if (a_controller->m_callbackMessageReceived)
    {
        a_controller->m_callbackMessageReceived(a_controller->m_callbackContext, a_record, a_message, a_length);
    }
    return TCP_RESULT_SUCCESS;
}


/* Called on the HANDLER worker thread. Same lock-free contract as the
 * other Process* functions. */
TcpResult
TcpServerController_ProcessDisconnect(TcpServerController* a_controller, TcpConnectionRecord* a_record)
{
    if (a_controller == NULL) return TCP_RESULT_NULL_PTR; 

    if (a_controller->m_callbackDisconnect)
    {
        a_controller->m_callbackDisconnect(a_controller->m_callbackContext, a_record);
    }

    LOG_DEBUG("disconnected from %s:%d (fd=%d)",
        a_record->m_ip, a_record->m_port, a_record->m_fdConnection);
    TcpConnectionRecord_Destroy(&a_record);

    return TCP_RESULT_SUCCESS;
}



TcpResult
TcpServerController_SetCallbacks(TcpServerController* a_controller,
    void* a_context,
    void (*a_callbackNewConnection)(void* a_context, const TcpConnectionRecord* a_record),
    void (*a_callbackDisconnect)(void* a_context, const TcpConnectionRecord* a_record),
    void (*a_callbackMessageReceived)(void* a_context, const TcpConnectionRecord* a_record, const char* a_message, size_t a_length))
{
    if (a_controller == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }
    LOG_DEBUG("Setting callbacks for server '%s'", a_controller->m_name);
    pthread_mutex_lock(&a_controller->m_lock);

    /* Reject mutation while workers are running. This is the linchpin that
     * makes the lock-free reads in ProcessConnection/Message/Disconnect
     * safe: callback pointers are guaranteed to be a frozen, fully-
     * published snapshot for the entire RUNNING phase. Returning an error
     * (rather than silently doing nothing) makes the misuse loud. */
    if (atomic_load(&a_controller->m_state) == SERVER_STATE_RUNNING)
    {
        pthread_mutex_unlock(&a_controller->m_lock);
        return TCP_RESULT_INVALID_ARGUMENT;
    }

    a_controller->m_callbackContext = a_context;
    a_controller->m_callbackNewConnection = a_callbackNewConnection;
    a_controller->m_callbackDisconnect = a_callbackDisconnect;
    a_controller->m_callbackMessageReceived = a_callbackMessageReceived;

    pthread_mutex_unlock(&a_controller->m_lock);
    return TCP_RESULT_SUCCESS;
}

uint16_t TcpServerController_GetPort(TcpServerController* a_controller)
{
    return a_controller->m_port;
}

uint32_t TcpServerController_GetIp(TcpServerController* a_controller)
{
    return a_controller->m_ip;
}

static char* CopyString(const char* a_string)
{
    char* result = (char*)malloc(strlen(a_string) + 1);
    if (result == NULL)
    {
        return NULL;
    }
    strcpy(result, a_string);
    return result;
}