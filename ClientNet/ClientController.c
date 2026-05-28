#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "ClientController.h"
#include "logger.h"
#include "network_utils.h"

typedef enum ClientState
{
    CLIENT_STATE_STOPPED,
    CLIENT_STATE_RUNNING,
} ClientState;

struct ClientController
{
    int m_fd;
    uint32_t m_ip;
    uint16_t m_port;
    ClientState m_state;
};

static ClientResult ConnectSocket(ClientController* a_controller);

ClientController* ClientController_Create(const char* a_ip, uint16_t a_port)
{
    ClientController* controller = (ClientController*)malloc(sizeof(ClientController));
    if (controller == NULL || a_ip == NULL || !is_valid_ip_address(a_ip))
    {
        return NULL;
    }
    controller->m_fd = -1;
    controller->m_ip = htonl(network_convert_ip_p_to_n(a_ip));
    controller->m_port = a_port;
    controller->m_state = CLIENT_STATE_STOPPED;
    return controller;
}

void ClientController_Destroy(ClientController** a_controller)
{
    if (a_controller == NULL || *a_controller == NULL)
    {
        return;
    }
    free(*a_controller);
    *a_controller = NULL;
}

ClientResult ClientController_Start(ClientController* a_controller)
{
    if (a_controller == NULL) return CLIENT_RESULT_NULL_PTR;
    if (a_controller->m_state != CLIENT_STATE_STOPPED) return CLIENT_RESULT_SUCCESS;

    ClientResult isConnected = ConnectSocket(a_controller);
    if (isConnected != CLIENT_RESULT_SUCCESS)
    {
        return isConnected;
    }

    a_controller->m_state = CLIENT_STATE_RUNNING;
    return CLIENT_RESULT_SUCCESS;
}

ClientResult 
ClientController_Stop(ClientController* a_controller)
{
    if (a_controller == NULL) return CLIENT_RESULT_NULL_PTR;
    if (a_controller->m_state != CLIENT_STATE_RUNNING) return CLIENT_RESULT_SUCCESS;
    a_controller->m_state = CLIENT_STATE_STOPPED;
    close(a_controller->m_fd);
    a_controller->m_fd = -1;
    return CLIENT_RESULT_SUCCESS;
}

ClientResult
ClientController_Send(ClientController *a_controller, const char *a_message, size_t a_length)
{
    if (a_controller == NULL || a_message == NULL || a_length == 0) return CLIENT_RESULT_NULL_PTR;
    if (a_controller->m_state != CLIENT_STATE_RUNNING) return CLIENT_RESULT_NOT_CONNECTED;
    LOG_DEBUG("Send: sending blocking message to server");
    if (send(a_controller->m_fd, a_message, a_length, 0) < 0)
    {
        LOG_ERROR("Send: send failed: %s", strerror(errno));
        return CLIENT_RESULT_SEND_ERROR;
    }
    LOG_INFO("Send: sent message to server");
    return CLIENT_RESULT_SUCCESS;
}

ClientResult
ClientController_Receive(ClientController* a_controller, char* a_buffer, size_t a_length)
{
    if (a_controller == NULL || a_buffer == NULL || a_length == 0) return CLIENT_RESULT_NULL_PTR;
    if (a_controller->m_state != CLIENT_STATE_RUNNING) return CLIENT_RESULT_NOT_CONNECTED;
    LOG_DEBUG("Receive: receiving blocking message from server");
    ssize_t bytes_received = recv(a_controller->m_fd, a_buffer, a_length, 0);

    if (bytes_received < 0)
    {
        LOG_ERROR("Receive: recv failed: %s", strerror(errno));
        return CLIENT_RESULT_SOCKET_ERROR; // <-- Your new error name
    }
    
    if (bytes_received == 0)
    {
        LOG_WARN("Receive: Server closed the connection");
        return CLIENT_RESULT_CONNECTION_CLOSED; 
    }

    LOG_INFO("Receive: received message from server (%zd bytes)", bytes_received);
    return CLIENT_RESULT_SUCCESS;
}

static ClientResult 
ConnectSocket(ClientController* a_controller)
{
    if (a_controller == NULL) return CLIENT_RESULT_NULL_PTR;
    
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return CLIENT_RESULT_SOCKET_ERROR;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(a_controller->m_port);
    addr.sin_addr.s_addr = a_controller->m_ip;

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) 
    {
        LOG_ERROR("Connect: connect failed: %s", strerror(errno));
        close(fd);
        return CLIENT_RESULT_CONNECT_ERROR;
    }
    LOG_INFO("Connect: connected to server");
    a_controller->m_fd = fd;
    return CLIENT_RESULT_SUCCESS;
}

