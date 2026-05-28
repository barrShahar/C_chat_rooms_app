#include <stdio.h>
#include "ClientApp.h"
#include "config.h"
#include "logger.h"

int
main(void)
{
    LOG_INFO("Starting Chat rooms client");
    ClientApp* app = ClientApp_Create(CONF_SERVER_IP, CONF_SERVER_PORT);
    if (app == NULL)
    {
        LOG_ERROR("Failed to create client app");
        return 1;
    }

    int rc = ClientApp_Run(app);

    LOG_INFO("Destroying client app");
    ClientApp_Destroy(&app);
    LOG_INFO("Client exited");
    return rc;
}
