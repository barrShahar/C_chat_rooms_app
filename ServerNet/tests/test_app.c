#include "../TcpServerController.h"
#include "../TcpConnectionRecord.h"
#include "../../utils/logger.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define SERVER_NAME "test_server"

static void CallbackNewConnection(const TcpConnectionRecord* a_record);
static void CallbackDisconnect(const TcpConnectionRecord* a_record);
static void CallbackMessageReceived(const TcpConnectionRecord* a_record, const char* a_message, size_t a_length);

int main(void)
{
    LOG_INFO("Starting test application");
    TcpServerController *tcp_server = TcpServerController_Create
    ("test_server", SERVER_IP, SERVER_PORT);

    TcpServerController_SetCallbacks(tcp_server, CallbackNewConnection, CallbackDisconnect, CallbackMessageReceived);

    TcpServerController_Start(tcp_server);


    // wait for user to press enter
    getchar();

    TcpServerController_Display(tcp_server);

    TcpServerController_Stop(tcp_server);

    TcpServerController_Destroy(&tcp_server);

    LOG_INFO("Test application finished");
    return 0;
}

static void
CallbackNewConnection(const TcpConnectionRecord* a_record)
{
    LOG_INFO("new connection: fd=%d ip=%s port=%d", a_record->m_fdConnection, a_record->m_ip, a_record->m_port);
}

static void
CallbackDisconnect(const TcpConnectionRecord* a_record)
{
    LOG_INFO("disconnect: fd=%d ip=%s port=%d", a_record->m_fdConnection, a_record->m_ip, a_record->m_port);
}

static void
CallbackMessageReceived(const TcpConnectionRecord* a_record, const char* a_message, size_t a_length)
{
    LOG_INFO("message from fd=%d ip=%s: %.*s", a_record->m_fdConnection, a_record->m_ip, (int)a_length, a_message);
}