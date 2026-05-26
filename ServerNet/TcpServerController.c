#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "TcpServerController.h"
#include "TcpConnectionAcceptor.h"
#include "network_utils.h"
#include "TcpConnectionHandler.h"
#include "TcpConnectionRecord.h"

static char* CopyString(const char* a_string);

typedef enum {
    SERVER_STATE_STOPPED,
    SERVER_STATE_RUNNING
} ServerState;

struct TcpServerController
{
    char* m_name;
    TcpConnectionAcceptor* m_connectionAcceptor;
    TcpConnectionHandler* m_connectionHandler;

    ServerState m_state;
    uint32_t m_ip;
    uint16_t m_port;

    // Callbacks
    void (*m_callbackNewConnection)(const TcpConnectionRecord* a_record);
    void (*m_callbackDisconnect)(const TcpConnectionRecord* a_record);
    void (*m_callbackMessageReceived)(const TcpConnectionRecord* a_record, const char* a_message, size_t a_length);
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
    controller->m_state = SERVER_STATE_STOPPED;

    controller->m_connectionAcceptor = TcpConnectionAcceptor_Create(controller);
    controller->m_connectionHandler = TcpConnectionHandler_Create(controller);

    if (controller->m_connectionAcceptor == NULL || controller->m_connectionHandler == NULL)
    {
        TcpServerController_Destroy(&controller);
        return NULL;
    }

    printf("Tcp server controller connected\n");
    return controller;
}

TcpResult 
TcpServerController_Display(TcpServerController* a_ctrl)
{
    if (a_ctrl == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }

    printf("TODO: Display the server controller\n");
    return TCP_RESULT_SUCCESS;
}

void 
TcpServerController_Destroy(TcpServerController** a_ctrl)
{
    if (a_ctrl == NULL || *a_ctrl == NULL)
    {
        return;
    }
    TcpServerController* controller = *a_ctrl;  // Alias

    // These functions safely handle pointers to NULL
    TcpConnectionAcceptor_Destroy(&controller->m_connectionAcceptor);
    TcpConnectionHandler_Destroy(&controller->m_connectionHandler);

    free(controller->m_name);
    free(controller);
    *a_ctrl = NULL;
}

TcpResult TcpServerController_Start(TcpServerController* a_ctrl)
{
    
    /*
    Start the CRS thread for the connection acceptor
    Start the DRS thread for the connection service manager
    */

    if (a_ctrl == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }

    if (a_ctrl->m_state == SERVER_STATE_RUNNING)
    {
        printf("Warning: Server is already running.\n");
        return TCP_RESULT_SUCCESS; // Or return a specific error like TCP_RESULT_ALREADY_RUNNING
    }

    if (TcpConnectionAcceptor_Start(a_ctrl->m_connectionAcceptor) != TCP_RESULT_SUCCESS 
        || TcpConnectionHandler_Start(a_ctrl->m_connectionHandler) != TCP_RESULT_SUCCESS)
    {
        return TCP_RESULT_THREAD_CREATION_FAILED;
    }

    a_ctrl->m_state = SERVER_STATE_RUNNING;
    return TCP_RESULT_SUCCESS;
}

TcpResult 
TcpServerController_ProcessConnection(TcpServerController* a_controller, TcpConnectionRecord* a_record)
{

    TcpResult resultHandlerDb =
        TcpConnectionHandler_AddConnection(a_controller->m_connectionHandler, a_record);

    if (resultHandlerDb != TCP_RESULT_SUCCESS)
    {
        // Delete record
    }

    printf("DEBUG:Controller_ProcessConnection: %s\n", a_record->m_ip);
    if (a_controller->m_callbackNewConnection)
    {
        a_controller->m_callbackNewConnection(a_record);
    }
    return TCP_RESULT_SUCCESS;
}

TcpResult
TcpServerController_ProcessDisconnect(TcpServerController* a_controller, TcpConnectionRecord* a_record)
{
    if (a_controller == NULL) { return TCP_RESULT_NULL_PTR; }

    if (a_controller->m_callbackDisconnect)
    {
        a_controller->m_callbackDisconnect(a_record);
    }

    TcpConnectionRecord_Destroy(&a_record);

    return TCP_RESULT_SUCCESS;
}

TcpResult
TcpServerController_ProcessMessage(TcpServerController* a_controller, const TcpConnectionRecord* a_record, const char* a_message, size_t a_length)
{
    if (a_controller == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }
    if (a_controller->m_callbackMessageReceived)
    {
        a_controller->
            m_callbackMessageReceived(a_record, a_message, a_length);
    }
        
    return TCP_RESULT_SUCCESS;
}

TcpResult 
TcpServerController_SetCallbacks(TcpServerController* a_controller, 
    void (*a_callbackNewConnection)(const TcpConnectionRecord* a_record), 
    void (*a_callbackDisconnect)(const TcpConnectionRecord* a_record), 
    void (*a_callbackMessageReceived)(const TcpConnectionRecord* a_record, const char* a_message, size_t a_length))
{
    if (a_controller == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }
    a_controller->m_callbackNewConnection = a_callbackNewConnection;
    a_controller->m_callbackDisconnect = a_callbackDisconnect;
    a_controller->m_callbackMessageReceived = a_callbackMessageReceived;
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