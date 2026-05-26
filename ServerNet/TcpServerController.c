#include <stdlib.h>
#include <string.h>
#include "TcpServerController.h"
#include "TcpConnectionAcceptor.h"
#include "network_utils.h"
#include "TcpConnectionHandler.h"
#include "TcpConnectionRecord.h"
#include "logger.h"

static char* CopyString(const char* a_string);

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
    if (a_name == NULL || a_ip == NULL)
    {
        LOG_ERROR("Invalid arguments: name or IP is NULL");
        return NULL;
    }
    if (!is_valid_ip_address(a_ip))
    {
        LOG_ERROR("Invalid IP address string format provided: %s", a_ip);
        return NULL;
    }

    TcpServerController* controller = (TcpServerController*)malloc(sizeof(TcpServerController));
    if (controller == NULL)
    {
        return NULL;
    }
    controller->m_name = CopyString(a_name);
    if (controller->m_name == NULL)
    {
        LOG_ERROR("Allocation error while copying server name");
        free(controller);
        return NULL;
    }
    controller->m_ip = ntohl(network_convert_ip_p_to_n(a_ip));
    controller->m_port = a_port;
    controller->m_state = SERVER_STATE_STOPPED;

    controller->m_connectionAcceptor = TcpConnectionAcceptor_Create(controller);
    controller->m_connectionHandler = TcpConnectionHandler_Create(controller);

    if (controller->m_connectionAcceptor == NULL || controller->m_connectionHandler == NULL)
    {
        TcpServerController_Destroy(&controller);   // Destroy function can handle NULL
        return NULL;
    }

    LOG_INFO("server '%s' created on %s:%d", a_name, a_ip, a_port);
    return controller;
}

void 
TcpServerController_Destroy(TcpServerController** a_ctrl)
{
    if (a_ctrl == NULL || *a_ctrl == NULL)
    {
        return;
    }
    TcpServerController* controller = *a_ctrl;  // Alias

    // Ensure the server is stopped before freeing memory 
    // This stops threads, closes listener sockets, etc.
    if (controller->m_state == SERVER_STATE_RUNNING)
    {
        TcpServerController_Stop(controller); 
    }

    // These functions safely handle pointers to NULL
    TcpConnectionAcceptor_Destroy(&controller->m_connectionAcceptor);
    TcpConnectionHandler_Destroy(&controller->m_connectionHandler);

    free(controller->m_name);
    free(controller);
    *a_ctrl = NULL;
}

TcpResult 
TcpServerController_Start(TcpServerController* a_ctrl)
{
    if (a_ctrl == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }

    if (a_ctrl->m_state == SERVER_STATE_RUNNING)
    {
        LOG_WARN("server '%s' is already running", a_ctrl->m_name);
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

void
TcpServerController_Stop(TcpServerController* a_ctrl)
{
    if (a_ctrl == NULL) 
    {
        return;
    }

    if (a_ctrl->m_state == SERVER_STATE_STOPPED)
    {
        LOG_WARN("server '%s' is already stopped", a_ctrl->m_name);
        return;
    }

    TcpConnectionAcceptor_Stop(a_ctrl->m_connectionAcceptor);
    TcpConnectionHandler_Stop(a_ctrl->m_connectionHandler);
    a_ctrl->m_state = SERVER_STATE_STOPPED;
    LOG_INFO("server '%s' stopped", a_ctrl->m_name);
}

TcpResult
TcpServerController_ProcessConnection(TcpServerController* a_controller,
    TcpConnectionRecord* a_record)
{
    if (a_controller == NULL || a_record == NULL)
    {
        return TCP_RESULT_NULL_PTR;
    }

    // 1. Add the connection to the handler
    TcpResult resultHandlerDb =
        TcpConnectionHandler_AddConnection(a_controller->m_connectionHandler, a_record);

    // 2. Handle if error occurred
    if (resultHandlerDb != TCP_RESULT_SUCCESS)
    {
        LOG_ERROR("failed to add connection to handler: %s", TcpResult_ToString(resultHandlerDb));
        return resultHandlerDb; // The acceptor must destroy the record
    }

    
    LOG_DEBUG("new connection from %s:%d (fd=%d)",
        a_record->m_ip, a_record->m_port, a_record->m_fdConnection);

    // 3. Notify the application about the new connection
    if (a_controller->m_callbackNewConnection)
    {
        a_controller->m_callbackNewConnection(a_record);
    }
    return TCP_RESULT_SUCCESS;
}

TcpResult 
TcpServerController_ProcessMessage(
    TcpServerController* a_controller, 
    const TcpConnectionRecord* a_record, 
    const char* a_message, size_t a_length)
{
    if (a_controller == NULL || a_record == NULL || a_message == NULL) return TCP_RESULT_NULL_PTR;
    if (a_length == 0) return TCP_RESULT_INVALID_ARGUMENT;

    if (a_controller->m_callbackMessageReceived)
    {
        a_controller->m_callbackMessageReceived(a_record, a_message, a_length);
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