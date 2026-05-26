#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "TcpConnectionAcceptor.h"
#include "TcpServerController.h"
#include "network_utils.h"
#include "TcpConnectionRecord.h"

typedef enum {
    ACCEPTOR_STATE_STOPPED,
    ACCEPTOR_STATE_RUNNING
} AcceptorState;

struct TcpConnectionAcceptor
{
    TcpServerController* m_tcpCtrl;
    pthread_t m_thread;
    int m_listenFd;
    AcceptorState m_state;
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
    acceptor->m_state = ACCEPTOR_STATE_STOPPED;

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
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(TcpServerController_GetPort(a_tcpCtrl));

    if (bind(acceptor->m_listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
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

void TcpConnectionAcceptor_Destroy(TcpConnectionAcceptor** a_acceptor)
{
    if (a_acceptor == NULL || *a_acceptor == NULL)
    {
        return;
    }
    free(*a_acceptor);
    *a_acceptor = NULL;
}



TcpResult TcpConnectionAcceptor_Start(TcpConnectionAcceptor* a_acceptor)
  {

    if (a_acceptor == NULL) 
    { 
        return TCP_RESULT_NULL_PTR; 
    }
    if (a_acceptor->m_state == ACCEPTOR_STATE_RUNNING)
    {
        return TCP_RESULT_SUCCESS;
    }

    int err = pthread_create(&a_acceptor->m_thread, NULL, TcpConnectionAcceptor_AcceptLoop, a_acceptor);
    if (err != 0)
    {
        return TCP_RESULT_THREAD_CREATION_FAILED;
    }

    return TCP_RESULT_SUCCESS;
  }

static void* TcpConnectionAcceptor_AcceptLoop(void* a_acceptor)
{
    TcpConnectionAcceptor* acceptor = (TcpConnectionAcceptor*)a_acceptor;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    printf("Acceptor thread is on listen\n");
    while (1)
    {
        int fdConnectionToClient = accept(acceptor->m_listenFd, 
            (struct sockaddr *)&client_addr, 
            &addr_len);

        if (fdConnectionToClient < 0)
        {
            fprintf(stderr, "TcpConnectionAcceptor_AcceptLoop: accept failed: %s\n", strerror(errno));
            continue;
        }

        static char ipBuffer[16];
        network_convert_ip_n_to_p(client_addr.sin_addr.s_addr, ipBuffer);
        
        
        TcpConnectionRecord* record = malloc(sizeof(TcpConnectionRecord));
        if (record == NULL)
        {
            fprintf(stderr, "TcpConnectionAcceptor_AcceptLoop: allocation of TCP record failed: %s\n", strerror(errno));
            continue;
        }

        record->m_fdConnection = fdConnectionToClient;
        strcpy(record->m_ip, ipBuffer);
        record->m_port = ntohs(client_addr.sin_port);
        
        TcpServerController_ProcessConnection(acceptor->m_tcpCtrl, record);
        printf("TcpConnectionAcceptor_AcceptLoop: new client connected: %d [%s:%d]\n", 
            fdConnectionToClient, 
            ipBuffer,
            ntohs(client_addr.sin_port));
    }
    
    return NULL;
}