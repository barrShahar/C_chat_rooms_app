#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include "TcpConnectionAcceptor.h"
#include "TcpServerController.h"
#include "network_utils.h"
#include "logger.h"
#include "TcpConnectionRecord.h"

/* ----------------------------------------------------------------------------
 * Threading model
 * ----------------------------------------------------------------------------
 * Two threads touch a TcpConnectionAcceptor instance:
 *
 *   1. The "owner" thread (typically the application's main thread) drives the
 *      lifecycle: Create / Start / Stop / Destroy. These calls are serialized
 *      by the parent TcpServerController's mutex, so the acceptor itself does
 *      not need its own lock.
 *
 *   2. The acceptor worker thread, spawned in Start(), runs AcceptLoop(). It
 *      blocks in accept() on m_listenFd and only ever reads m_state.
 *
 * Shared state between the two threads:
 *   - m_state:    written by the owner (Start/Stop), read by the worker.
 *                 Declared _Atomic so the read in the loop is a well-defined
 *                 cross-thread observation (no torn reads, no compiler hoisting
 *                 the load out of the loop).
 *   - m_listenFd: written once during Create, then read by both threads.
 *                 Stop() calls shutdown(m_listenFd) to unblock the worker's
 *                 accept(). close() must happen ONLY after pthread_join, so we
 *                 never close a fd that the worker might still be reading from.
 *   - m_thread:   written once by Start (pthread_create), read by Stop
 *                 (pthread_join). No concurrent access because Start and Stop
 *                 are themselves serialized by the controller's mutex.
 * ---------------------------------------------------------------------------- */

typedef enum {
    ACCEPTOR_STATE_STOPPED,
    ACCEPTOR_STATE_RUNNING
} AcceptorState;

struct TcpConnectionAcceptor
{
    TcpServerController* m_tcpCtrl;
    pthread_t m_thread;
    int m_listenFd;
    /* _Atomic: read by the acceptor worker every loop iteration, written by
     * the owner thread in Start/Stop. Default memory order is seq-cst, which
     * is overkill for a single flag but trivially correct and not on a hot path. */
    _Atomic AcceptorState m_state;
};
// statics functions declarations
static void* TcpConnectionAcceptor_AcceptLoop(void* a_acceptor);


TcpConnectionAcceptor* TcpConnectionAcceptor_Create(TcpServerController* a_tcpCtrl)
{
    if (a_tcpCtrl == NULL) return NULL;

    TcpConnectionAcceptor* acceptor = (TcpConnectionAcceptor*)malloc(sizeof(TcpConnectionAcceptor));
    if (acceptor == NULL)
    {
        return NULL;
    }
    
    acceptor->m_tcpCtrl = a_tcpCtrl;
    acceptor->m_thread = 0;
    /* Initialize the atomic before any other thread can observe it. Safe here
     * because no worker thread exists yet. */
    atomic_store(&acceptor->m_state, ACCEPTOR_STATE_STOPPED);

    // 1. Create the socket
    acceptor->m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (acceptor->m_listenFd < 0)
    {
        free(acceptor);
        return NULL;
    }

    // 2. Set SO_REUSEADDR so restarts don't crash with "Address already in use"
    int opt = 1;
    if (setsockopt(acceptor->m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        close(acceptor->m_listenFd);
        free(acceptor);
        return NULL;
    }

    // 2.1 
    if (setsockopt(acceptor->m_listenFd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        close(acceptor->m_listenFd);
        free(acceptor);
        return NULL;
    }
    
    // 3. Prepare the address struct and Bind
    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = TcpServerController_GetIp(a_tcpCtrl);
    addr.sin_port        = htons(TcpServerController_GetPort(a_tcpCtrl));

    if (bind(acceptor->m_listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        LOG_ERROR("bind failed on port %d: %s", TcpServerController_GetPort(a_tcpCtrl), strerror(errno));
        close(acceptor->m_listenFd);
        free(acceptor);
        return NULL;
    }

    // 4. Enter Listen Mode. 
    if (listen(acceptor->m_listenFd, SOMAXCONN) < 0)
    {
        close(acceptor->m_listenFd);
        free(acceptor);
        return NULL;
    }

    return acceptor;
}

void TcpConnectionAcceptor_Stop(TcpConnectionAcceptor* a_acceptor)
{
    if (a_acceptor == NULL)
    {
        return;
    }

    /* Idempotency guard via atomic compare-and-swap-ish exchange.
     *
     * atomic_exchange() returns the OLD value while installing STOPPED.
     * We treat this as "only the first caller that sees RUNNING gets to do
     * the join+close". A second call (or a concurrent call from another
     * thread) will observe STOPPED here and return immediately, which is
     * critical because pthread_join() on an already-joined thread is
     * undefined behavior, and close() on an already-closed fd risks closing
     * an unrelated fd that the OS may have reassigned. */
    if (atomic_exchange(&a_acceptor->m_state, ACCEPTOR_STATE_STOPPED) != ACCEPTOR_STATE_RUNNING)
    {
        return;
    }

    /* The worker is blocked inside accept(). shutdown() makes the kernel
     * tear down the listening socket and return -1 from accept() with errno
     * set, which the loop checks and breaks on (it re-checks m_state, sees
     * STOPPED, and exits). We MUST NOT close() the fd here yet because the
     * worker may still be inside the syscall referencing it. */
    shutdown(a_acceptor->m_listenFd, SHUT_RDWR);

    /* Wait for the worker to finish its current loop iteration and return.
     * After join() returns, no other thread can be observing m_listenFd. */
    pthread_join(a_acceptor->m_thread, NULL);

    /* Safe to close now: the worker thread is gone. */
    close(a_acceptor->m_listenFd);

    LOG_INFO("Acceptor thread stopped");
}

void TcpConnectionAcceptor_Destroy(TcpConnectionAcceptor** a_acceptor)
{
    if (a_acceptor == NULL || *a_acceptor == NULL)
    {
        return;
    }
    TcpConnectionAcceptor_Stop(*a_acceptor);
    free(*a_acceptor);
    *a_acceptor = NULL;
}



TcpResult TcpConnectionAcceptor_Start(TcpConnectionAcceptor* a_acceptor)
  {

    if (a_acceptor == NULL) 
    { 
        return TCP_RESULT_NULL_PTR; 
    }
    if (atomic_load(&a_acceptor->m_state) == ACCEPTOR_STATE_RUNNING)
    {
        return TCP_RESULT_SUCCESS;
    }

    /* Publish RUNNING BEFORE pthread_create so the new worker sees RUNNING on
     * its very first read of m_state. If we set it after pthread_create, the
     * worker could conceivably observe STOPPED and exit immediately. */
    atomic_store(&a_acceptor->m_state, ACCEPTOR_STATE_RUNNING);
    int err = pthread_create(&a_acceptor->m_thread, NULL, TcpConnectionAcceptor_AcceptLoop, a_acceptor);
    if (err != 0)
    {
        /* pthread_create failed: roll the state back so a subsequent Start
         * can try again, and so a future Stop() doesn't try to join a
         * thread that never existed. */
        atomic_store(&a_acceptor->m_state, ACCEPTOR_STATE_STOPPED);
        return TCP_RESULT_THREAD_CREATION_FAILED;
    }

    return TCP_RESULT_SUCCESS;
  }

/* Worker-thread entry point. Runs entirely on the acceptor thread spawned
 * in Start(). Touches only:
 *   - acceptor->m_state    (atomic read)
 *   - acceptor->m_listenFd (read-only after Create)
 *   - acceptor->m_tcpCtrl  (read-only after Create; thread-safety of any
 *                           method we call on it is the controller's problem) */
static void* TcpConnectionAcceptor_AcceptLoop(void* a_acceptor)
{
    TcpConnectionAcceptor* acceptor = (TcpConnectionAcceptor*)a_acceptor;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    LOG_INFO("Acceptor thread is on listen");
    /* atomic_load every iteration: Stop() flips this to STOPPED to terminate
     * the loop. Without _Atomic, the compiler would be free to hoist this
     * read out of the loop (since the loop body doesn't write m_state),
     * turning a graceful shutdown into an infinite loop. */
    while (atomic_load(&acceptor->m_state) == ACCEPTOR_STATE_RUNNING)
    {
        /* Blocking syscall. Stop() unblocks us via shutdown(m_listenFd). */
        int fdConnectionToClient = accept(acceptor->m_listenFd,
            (struct sockaddr *)&client_addr,
            &addr_len);

        if (fdConnectionToClient < 0)
        {
            /* Distinguish "we were asked to stop" (expected, exit silently)
             * from "the syscall genuinely failed" (log and retry). */
            if (atomic_load(&acceptor->m_state) == ACCEPTOR_STATE_STOPPED) break;
            LOG_ERROR("TcpConnectionAcceptor_AcceptLoop: accept failed: %s", strerror(errno));
            continue;
        }

        // 2. Create the record
        char ipBuffer[16];
        network_convert_ip_n_to_p(client_addr.sin_addr.s_addr, ipBuffer);
        uint16_t clientPort = ntohs(client_addr.sin_port);

        TcpConnectionRecord* record = TcpConnectionRecord_Create(
            fdConnectionToClient, ipBuffer, clientPort);
        if (record == NULL)
        {
            LOG_ERROR("TcpConnectionAcceptor_AcceptLoop: record allocation failed");
            close(fdConnectionToClient);
            continue;
        }

        /* 3. Hand the record off to the controller, which will forward it to
         * the handler via a pipe write. Ownership transfers on success. The
         * pipe write of a sizeof(void*) blob is atomic per POSIX, so no lock
         * is needed for the cross-thread handoff itself. */
        TcpResult result = TcpServerController_ProcessConnection(
            acceptor->m_tcpCtrl, record);
        if (result != TCP_RESULT_SUCCESS)
        {
            LOG_ERROR("TcpConnectionAcceptor_AcceptLoop: failed to process connection: %s",
                TcpResult_ToString(result));
            TcpConnectionRecord_Destroy(&record);
            continue;
        }

        LOG_INFO("TcpConnectionAcceptor_AcceptLoop: new client connected: %d [%s:%d]",
            fdConnectionToClient, ipBuffer, clientPort);
    }
    
    return NULL;
}