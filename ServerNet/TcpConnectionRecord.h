#pragma once
#include <stdint.h>
#include <arpa/inet.h>   /* INET_ADDRSTRLEN */

typedef struct TcpConnectionRecord
{
    int m_fdConnection;
    char m_ip[INET_ADDRSTRLEN];
    int m_port;
} TcpConnectionRecord;

TcpConnectionRecord* TcpConnectionRecord_Create(int a_fdConnection, const char* a_ip, uint16_t a_port);
void TcpConnectionRecord_Display(TcpConnectionRecord* a_record);

void TcpConnectionRecord_Destroy(TcpConnectionRecord** a_record);