#ifndef __CLIENT_APP_H__
#define __CLIENT_APP_H__

#include <stdint.h>

typedef struct ClientApp ClientApp;

ClientApp* ClientApp_Create(const char* a_ip, uint16_t a_port);
void       ClientApp_Destroy(ClientApp** a_app);
int        ClientApp_Run(ClientApp* a_app);

#endif /* __CLIENT_APP_H__ */
