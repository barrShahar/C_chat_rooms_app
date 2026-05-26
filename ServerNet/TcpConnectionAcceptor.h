#pragma once
#include "TcpServerController.h"

typedef struct TcpConnectionAcceptor TcpConnectionAcceptor;

TcpConnectionAcceptor* TcpConnectionAcceptor_Create(TcpServerController* a_tcpCtrl);
void TcpConnectionAcceptor_Destroy(TcpConnectionAcceptor** a_acceptor);

TcpResult TcpConnectionAcceptor_Start(TcpConnectionAcceptor* a_acceptor);
