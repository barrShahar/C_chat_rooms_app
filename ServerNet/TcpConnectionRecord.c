#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "TcpConnectionRecord.h"

void 
TcpConnectionRecord_Display(TcpConnectionRecord* a_record)
{
    printf("fd: %d, ip: %s, port: %d\n", a_record->m_fdConnection, a_record->m_ip, a_record->m_port);
}

void 
TcpConnectionRecord_Destroy(TcpConnectionRecord** a_record)
{
    if (a_record == NULL || *a_record == NULL) { return; }
    if ((*a_record)->m_fdConnection >= 0)
    {
        close((*a_record)->m_fdConnection);
    }
    
    free(*a_record);
    *a_record = NULL;
}

TcpConnectionRecord* 
TcpConnectionRecord_Create(int a_fdConnection, const char* a_ip, uint16_t a_port)
{
    TcpConnectionRecord* record = (TcpConnectionRecord*)malloc(sizeof(TcpConnectionRecord));
    if (record == NULL)
    {
        return NULL;
    }
    record->m_fdConnection = a_fdConnection;
    strcpy(record->m_ip, a_ip);
    record->m_port = a_port;
    return record;
}
