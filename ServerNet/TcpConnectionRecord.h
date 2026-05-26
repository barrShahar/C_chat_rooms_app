#pragma once

typedef struct TcpConnectionRecord
{
    int m_fdConnection;
    char m_ip[16];
    int m_port;
} TcpConnectionRecord;

void TcpConnectionRecord_Display(TcpConnectionRecord* a_record);

void TcpConnectionRecord_Destroy(TcpConnectionRecord** a_record);