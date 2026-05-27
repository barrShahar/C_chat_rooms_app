#include <stdlib.h>
#include "ServerManager.h"
#include "UserManager.h"
#include "GroupManager.h"
#include "TcpServerController.h"
#include "logger.h"
#include "config.h"

struct ServerManager
{
    UserManager* m_userManager;
    GroupManager* m_groupManager;
    TcpServerController* m_tcpServerController;
};

// call back functions for TcpServerController
static void onNewConnection(const TcpConnectionRecord* a_record);
static void onDisconnect(const TcpConnectionRecord* a_record);
static void onMessageReceived(const TcpConnectionRecord* a_record, const char* a_message, size_t a_length);


ServerManager*
ServerManager_Create(void)
{
    ServerManager* serverManager = (ServerManager*)malloc(sizeof(ServerManager));
    if (serverManager == NULL)
    {
        return NULL;
    }
    serverManager->m_userManager = UserManager_Create();
    if (serverManager->m_userManager == NULL)
    {
        free(serverManager);
        return NULL;
    }
    serverManager->m_groupManager = GroupManager_Create();
    if (serverManager->m_groupManager == NULL)
    {
        free(serverManager);
        return NULL;
    }
    serverManager->m_tcpServerController = TcpServerController_Create("Chat rooms server", CONF_SERVER_IP, CONF_SERVER_PORT);
    if (serverManager->m_tcpServerController == NULL)
    {
        free(serverManager);
        return NULL;
    }

    if (TcpServerController_SetCallbacks(serverManager->m_tcpServerController,
         onNewConnection,
          onDisconnect,
           onMessageReceived) != TCP_RESULT_SUCCESS)
    {
        free(serverManager);
        return NULL;
    }
    

    return serverManager;
}

void ServerManager_Destroy(ServerManager** a_manager)
{
    if (a_manager == NULL || *a_manager == NULL)
    {
        return;
    }

    TcpServerController_Destroy(&(*a_manager)->m_tcpServerController);
    GroupManager_Destroy(&(*a_manager)->m_groupManager);
    UserManager_Destroy(&(*a_manager)->m_userManager);
    free(*a_manager);
    *a_manager = NULL;
}

ServerResult ServerManager_Start(ServerManager* a_serverManager)
{
    if (TcpServerController_Start(a_serverManager->m_tcpServerController) != TCP_RESULT_SUCCESS)
    {
        return SERVER_RESULT_NETWORK_ERROR;
    }
    return SERVER_RESULT_SUCCESS;
}


static void onNewConnection(const TcpConnectionRecord* a_record)
{
    LOG_INFO("New connection from %s:%d", a_record->m_ip, a_record->m_port);
}
static void onDisconnect(const TcpConnectionRecord* a_record)
{
    LOG_INFO("Disconnection from %s:%d", a_record->m_ip, a_record->m_port);
}
static void onMessageReceived(const TcpConnectionRecord* a_record, const char* a_message, size_t a_length)
{
    (void)a_length;
    LOG_INFO("Message received from %s:%d: %s", a_record->m_ip, a_record->m_port, a_message);
}