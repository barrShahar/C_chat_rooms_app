#include "TcpServerController.h"
#include "TcpConnectionRecord.h"
#include <stdio.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define SERVER_NAME "test_server"

static void PrintRecord(const TcpConnectionRecord* a_record);
static void CallbackNewConnection(const TcpConnectionRecord* a_record);
static void CallbackDisconnect(const TcpConnectionRecord* a_record);
static void CallbackMessageReceived(const TcpConnectionRecord* a_record, const char* a_message, size_t a_length);

int main(void)
{

    TcpServerController *tcp_server = TcpServerController_Create
    ("test_server", SERVER_IP, SERVER_PORT);

    TcpServerController_SetCallbacks(tcp_server, CallbackNewConnection, CallbackDisconnect, CallbackMessageReceived);

    TcpServerController_Start(tcp_server);


    // wait for user to press enter
    getchar();

    TcpServerController_Display(tcp_server);
    return 0;
}

static void
PrintRecord(const TcpConnectionRecord* a_record)
{
    printf("fd: %d, ip: %s, port: %d\n", a_record->m_fdConnection, a_record->m_ip, a_record->m_port);
}

static void 
CallbackNewConnection(const TcpConnectionRecord* a_record)
{
    printf("App: New connection: ");
    PrintRecord(a_record);
}

static void 
CallbackDisconnect(const TcpConnectionRecord* a_record)
{
    printf("App: Disconnect: ");
    PrintRecord(a_record);
}

static void 
CallbackMessageReceived(const TcpConnectionRecord* a_record, const char* a_message, size_t a_length)
{
    printf("App: Message from ");
    PrintRecord(a_record);
    printf("  msg(%zu): %.*s\n", a_length, (int)a_length, a_message);
}