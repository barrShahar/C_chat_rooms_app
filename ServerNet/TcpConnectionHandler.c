#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include "TcpConnectionHandler.h"
#include "TcpServerController.h"
#include "../db/gen_dlist.h"
#include "logger.h"

#define RECV_BUF_SIZE 4096

typedef enum {
    HANDLER_STATE_STOPPED,
    HANDLER_STATE_RUNNING
} HandlerState;

struct TcpConnectionHandler
{
    TcpServerController* m_tcpCtrl;
    pthread_t m_thread;
    HandlerState m_state;
    List* m_connectionsDB;

    // For select design pattern
    int m_maxFd;
    fd_set m_activeFdSet;
    fd_set m_activeFdSetCopy;

    // Self-pipe: write a TcpConnectionRecord* through [1] to wake up select() on [0]
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
static void CheckForNewConnections(TcpConnectionHandler* handler, int* nReady);
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
    handler->m_state = HANDLER_STATE_STOPPED;
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

    if (a_handler->m_state == HANDLER_STATE_RUNNING)
    {
        return TCP_RESULT_SUCCESS;
    }

    int result = pthread_create(&a_handler->m_thread, NULL, ClientHandlerIOLoop, a_handler);
    if (result > 0) // Any value greater than 0 indicates a POSIX error code
    {
        LOG_ERROR("pthread_create failed: %s", strerror(result));
        return TCP_RESULT_THREAD_CREATION_FAILED;
    }
    
    a_handler->m_state = HANDLER_STATE_RUNNING;
    return TCP_RESULT_SUCCESS;
}



void
TcpConnectionHandler_Stop(TcpConnectionHandler* a_handler)
{
    if (a_handler == NULL || a_handler->m_state != HANDLER_STATE_RUNNING)
    {
        return;
    }
    a_handler->m_state = HANDLER_STATE_STOPPED;
    TcpConnectionRecord* stop = NULL;
    write(a_handler->m_wakeupPipe[PIPE_WRITE], &stop, sizeof(stop));
    pthread_join(a_handler->m_thread, NULL);
    LOG_INFO("Handler thread stopped");
}

TcpResult
TcpConnectionHandler_AddConnection(TcpConnectionHandler* a_handler, TcpConnectionRecord* a_record)
{
    if (a_handler == NULL || a_record == NULL) 
    {
        return TCP_RESULT_NULL_PTR;
    }
    // Write the pointer through the pipe — wakes up select() in the IO loop
    ssize_t written = write(a_handler->m_wakeupPipe[PIPE_WRITE], &a_record, sizeof(a_record));
    return CHECK_WRITE_SIZE(written, sizeof(a_record));
}
static void ProcessNewConnection(TcpConnectionHandler* handler)
{
    TcpConnectionRecord* record;
    // Read the pointer passed through the pipe
    if (read(handler->m_wakeupPipe[PIPE_READ], &record, sizeof(record)) == sizeof(record))
    {
        if (record == NULL) return; // stop signal
        ListPushTail(handler->m_connectionsDB, record);
        FD_SET(record->m_fdConnection, &handler->m_activeFdSet);
        UPDATE_MAX_FD(handler->m_maxFd, record->m_fdConnection);
        LOG_DEBUG("connection fd=%d registered, maxFd=%d", record->m_fdConnection, handler->m_maxFd);
    }
}

static void 
ProcessClientData(TcpConnectionHandler* handler, TcpConnectionRecord* record, ListItr currentItr) 
{
    char buf[RECV_BUF_SIZE];
    ssize_t n = recv(record->m_fdConnection, buf, sizeof(buf), 0);

    if (n <= 0)
    {
        if (n == 0) {
            LOG_INFO("client fd=%d disconnected", record->m_fdConnection);
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

static void* 
ClientHandlerIOLoop(void* a_handler)
{
    TcpConnectionHandler* handler = (TcpConnectionHandler*)a_handler;

    while (handler->m_state == HANDLER_STATE_RUNNING)
    {
        int nReady = WaitForActivity(handler);
        
        if (nReady < 0) break;     // Break the loop on fatal errors
        if (nReady == 0) continue; // Loop again on timeout or interrupt

        // 1. Handle main thread tapping us on the shoulder
        CheckForNewConnections(handler, &nReady);
        
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
CheckForNewConnections(TcpConnectionHandler* handler, int* nReady) 
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