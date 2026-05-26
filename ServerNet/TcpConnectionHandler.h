#pragma once
#include "TcpServerController.h"

typedef struct TcpConnectionHandler TcpConnectionHandler;

TcpConnectionHandler* TcpConnectionHandler_Create(TcpServerController* a_tcpCtrl);
void TcpConnectionHandler_Destroy(TcpConnectionHandler** a_handler);

TcpResult TcpConnectionHandler_Start(TcpConnectionHandler* a_handler);
void TcpConnectionHandler_Stop(TcpConnectionHandler* a_handler);

TcpResult TcpConnectionHandler_AddConnection(TcpConnectionHandler* a_handler, TcpConnectionRecord* a_record);
