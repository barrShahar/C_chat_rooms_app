#include <stdio.h>
#include "ServerManager.h"
#include "TcpServerController.h"
#include "logger.h"
#include "config.h"

int 
main(void)
{
    LOG_INFO("Starting Chat rooms server");
    ServerManager* serverManager = ServerManager_Create(CONF_SERVER_NAME, CONF_SERVER_IP, CONF_SERVER_PORT);
    if (serverManager == NULL)
    {
        LOG_ERROR("Failed to create server manager");
        return 1;
    }

    if (ServerManager_Start(serverManager) != SERVER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to start server manager");
        return 1;
    }
    
    getchar();
    
    LOG_INFO("Stopping server manager");
    ServerManager_Stop(serverManager);
    LOG_INFO("Destroying server manager");
    ServerManager_Destroy(&serverManager);
    LOG_INFO("Server manager destroyed");

    return 0;
}