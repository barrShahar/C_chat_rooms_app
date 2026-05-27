#include <stdio.h>
#include "ServerManager.h"
#include "TcpServerController.h"
#include "logger.h"
#include "config.h"

int 
main(void)
{
    LOG_INFO("Starting Chat rooms server");
    ServerManager* serverManager = ServerManager_Create();
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
    

    ServerManager_Destroy(&serverManager);
    LOG_INFO("Server manager destroyed");

    return 0;
}