#include <errno.h>
#include <string.h>
#include <sys/socket.h>

#include "../TcpServerController.h"
#include "../TcpConnectionRecord.h"
#include "../../utils/logger.h"
#include "../../config.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define SERVER_NAME "test_server"

static void CallbackNewConnection(void* a_context, const TcpConnectionRecord* a_record);
static void CallbackDisconnect(void* a_context, const TcpConnectionRecord* a_record);
static void CallbackMessageReceived(void* a_context, const TcpConnectionRecord* a_record, const char* a_message, size_t a_length);

int main(void)
{
    LOG_INFO("Starting test application");
    TcpServerController *tcp_server = TcpServerController_Create(
        SERVER_NAME, SERVER_IP, SERVER_PORT);
    if (tcp_server == NULL)
    {
        LOG_ERROR("TcpServerController_Create failed");
        return 1;
    }

    if (TcpServerController_SetCallbacks(tcp_server, NULL,
            CallbackNewConnection, CallbackDisconnect, CallbackMessageReceived) != TCP_RESULT_SUCCESS)
    {
        LOG_ERROR("TcpServerController_SetCallbacks failed");
        TcpServerController_Destroy(&tcp_server);
        return 1;
    }

    if (TcpServerController_Start(tcp_server) != TCP_RESULT_SUCCESS)
    {
        LOG_ERROR("TcpServerController_Start failed");
        TcpServerController_Destroy(&tcp_server);
        return 1;
    }

    LOG_INFO("Server listening on %s:%d — press Enter to stop", SERVER_IP, SERVER_PORT);
    getchar();
    LOG_INFO("Stopping server");
    TcpServerController_Stop(tcp_server);
    LOG_INFO("Destroying server");
    TcpServerController_Destroy(&tcp_server);
    LOG_INFO("Server destroyed");

    LOG_INFO("Test application finished");
    return 0;
}

static void
CallbackNewConnection(void* a_context, const TcpConnectionRecord* a_record)
{
    (void)a_context;
    LOG_INFO("new connection: fd=%d ip=%s port=%d", a_record->m_fdConnection, a_record->m_ip, a_record->m_port);
}

static void
CallbackDisconnect(void* a_context, const TcpConnectionRecord* a_record)
{
    (void)a_context;
    LOG_INFO("disconnect: fd=%d ip=%s port=%d", a_record->m_fdConnection, a_record->m_ip, a_record->m_port);
}

static void
CallbackMessageReceived(void* a_context, const TcpConnectionRecord* a_record, const char* a_message, size_t a_length)
{
    (void)a_context;
    LOG_INFO("message from fd=%d ip=%s: %.*s", a_record->m_fdConnection, a_record->m_ip, (int)a_length, a_message);

    char reply[CONF_RECV_BUF_SIZE];
    int reply_len = snprintf(reply, sizeof(reply), "Echo: %.*s", (int)a_length, a_message);
    if (send(a_record->m_fdConnection, reply, (size_t)reply_len, 0) < 0)
    {
        LOG_ERROR("send echo failed on fd=%d: %s", a_record->m_fdConnection, strerror(errno));
    }
}