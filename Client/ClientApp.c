#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ClientApp.h"
#include "Menu.h"
#include "ClientController.h"
#include "NetworkProtocol.h"
#include "config.h"
#include "logger.h"     

struct ClientApp
{
    ClientController* m_controller;
    SessionState      m_state;
    char              m_ip[32];
    uint16_t          m_port;
};

ClientApp* ClientApp_Create(const char* a_ip, uint16_t a_port)
{
    if (a_ip == NULL) return NULL;
    if (strlen(a_ip) >= sizeof(((ClientApp*)0)->m_ip)) return NULL;

    ClientApp* app = (ClientApp*)malloc(sizeof(ClientApp));
    if (app == NULL) return NULL;

    app->m_controller = ClientController_Create(a_ip, a_port);
    if (app->m_controller == NULL)
    {
        LOG_ERROR("Failed to create client controller");
        free(app);
        return NULL;
    }
    app->m_state = SESSION_DISCONNECTED;
    strcpy(app->m_ip, a_ip);
    app->m_port = a_port;
    LOG_INFO("ClientApp created");
    return app;
}

void ClientApp_Destroy(ClientApp** a_app)
{
    if (a_app == NULL || *a_app == NULL) return;
    if ((*a_app)->m_state != SESSION_DISCONNECTED)
    {
        ClientController_Stop((*a_app)->m_controller);
    }
    ClientController_Destroy(&(*a_app)->m_controller);
    free(*a_app);
    *a_app = NULL;
    LOG_INFO("ClientApp destroyed");
}

/* Local string mapper for ClientResult. ClientController.h declares
 * ClientResult_ToString but it has no implementation in this tree — using a
 * private mapper avoids depending on an unimplemented symbol. */
static const char* ClientResultStr(ClientResult a_result)
{
    switch (a_result)
    {
        case CLIENT_RESULT_SUCCESS:           return "success";
        case CLIENT_RESULT_NULL_PTR:          return "null pointer";
        case CLIENT_RESULT_INVALID_ARGUMENT:  return "invalid argument";
        case CLIENT_RESULT_ALLOCATION_FAILED: return "allocation failed";
        case CLIENT_RESULT_SOCKET_ERROR:      return "socket error";
        case CLIENT_RESULT_CONNECT_ERROR:     return "connect error";
        case CLIENT_RESULT_NOT_CONNECTED:     return "not connected";
        case CLIENT_RESULT_RECEIVE_ERROR:     return "receive error";
        case CLIENT_RESULT_CONNECTION_CLOSED: return "connection closed";
        case CLIENT_RESULT_SEND_ERROR:        return "send error";
        default:                              return "unknown";
    }
}

static const char* StatusToString(ChatStatus a_status)
{
    switch (a_status)
    {
        case CHAT_OK:              return "OK";
        case CHAT_ERR_GENERIC:     return "Generic error";
        case CHAT_ERR_BAD_CREDS:   return "Bad credentials";
        case CHAT_ERR_NAME_TAKEN:  return "Name already taken";
        case CHAT_ERR_NOT_FOUND:   return "Not found";
        case CHAT_ERR_ALREADY_IN:  return "Already in group";
        case CHAT_ERR_NOT_IN:      return "Not in group";
        case CHAT_ERR_NOT_LOGGED:  return "Not logged in";
        case CHAT_ERR_ALREADY_LOG: return "Already logged in";
        case CHAT_ERR_MALFORMED:   return "Malformed message";
        case CHAT_ERR_NULL_PTR:    return "Null pointer";
        default:                   return "Unknown";
    }
}

/* ------------------------------------------------------------------------- */
/* Send + receive one request/response round trip.                            */
/* Returns true if a well-formed response was received and out_resp is set.  */
/* On CONNECTION_CLOSED, sets *a_outClosed to true.                          */
/* ------------------------------------------------------------------------- */
static bool ExchangeMessage(ClientApp* a_app, const ChatMessage* a_req,
                            ChatMessage* a_outResp, bool* a_outClosed)
{
    *a_outClosed = false;

    char sendBuf[CHAT_MAX_TOTAL];
    int sentLen = SerializeChatMessage(a_req, sendBuf, sizeof(sendBuf));
    if (sentLen < 0)
    {
        printf("Internal error: failed to serialize message\n");
        return false;
    }

    ClientResult sendResult = ClientController_Send(a_app->m_controller, sendBuf, (size_t)sentLen);
    if (sendResult != CLIENT_RESULT_SUCCESS)
    {
        printf("Send failed: %s\n", ClientResultStr(sendResult));
        if (sendResult == CLIENT_RESULT_NOT_CONNECTED) *a_outClosed = true;
        return false;
    }

    char recvBuf[CONF_RECV_BUF_SIZE];
    ClientResult recvResult = ClientController_Receive(a_app->m_controller, recvBuf, sizeof(recvBuf));
    if (recvResult == CLIENT_RESULT_CONNECTION_CLOSED)
    {
        printf("Server closed the connection.\n");
        *a_outClosed = true;
        return false;
    }
    if (recvResult != CLIENT_RESULT_SUCCESS)
    {
        printf("Receive failed: %s\n", ClientResultStr(recvResult));
        return false;
    }

    if (DeserializeChatMessage(recvBuf, sizeof(recvBuf), a_outResp) != CHAT_OK)
    {
        printf("Received malformed response.\n");
        return false;
    }

    if (a_outResp->m_opcode != OPCODE_RESPONSE)
    {
        printf("Unexpected response opcode: 0x%02x\n", a_outResp->m_opcode);
        return false;
    }
    return true;
}

static void PrintResponse(const ChatMessage* a_resp)
{
    if (a_resp->m_status == CHAT_OK)
    {
        if (a_resp->m_length > 0)
        {
            printf("OK: %.*s\n", (int)a_resp->m_length, a_resp->m_value);
        }
        else
        {
            printf("OK\n");
        }
    }
    else
    {
        printf("Error: %s", StatusToString(a_resp->m_status));
        if (a_resp->m_length > 0)
        {
            printf(" (%.*s)", (int)a_resp->m_length, a_resp->m_value);
        }
        printf("\n");
    }
}

static void HandleConnect(ClientApp* a_app)
{
    ClientResult result = ClientController_Start(a_app->m_controller);
    if (result != CLIENT_RESULT_SUCCESS)
    {
        printf("Connect failed: %s\n", ClientResultStr(result));
        return;
    }
    a_app->m_state = SESSION_CONNECTED;
    printf("Connected to %s:%u\n", a_app->m_ip, (unsigned)a_app->m_port);
}

static bool ReadCredentials(char* a_name, size_t a_nameSize,
                            char* a_password, size_t a_passwordSize)
{
    if (!Menu_ReadLine("  username: ", a_name, a_nameSize)) return false;
    if (a_name[0] == '\0')
    {
        printf("Username cannot be empty.\n");
        return false;
    }
    if (!Menu_ReadLine("  password: ", a_password, a_passwordSize)) return false;
    if (a_password[0] == '\0')
    {
        printf("Password cannot be empty.\n");
        return false;
    }
    return true;
}

static void PackCredentials(ChatMessage* a_msg, const char* a_name, const char* a_password)
{
    size_t nameLen = strlen(a_name);
    size_t passLen = strlen(a_password);
    memcpy(a_msg->m_value, a_name, nameLen + 1);
    memcpy(a_msg->m_value + nameLen + 1, a_password, passLen + 1);
    a_msg->m_length = (uint16_t)(nameLen + passLen + 2);
}

static bool ReadGroupName(char* a_name, size_t a_size)
{
    if (!Menu_ReadLine("  group name: ", a_name, a_size)) return false;
    if (a_name[0] == '\0')
    {
        printf("Group name cannot be empty.\n");
        return false;
    }
    return true;
}

static void HandleCredentialedAction(ClientApp* a_app, MessageOpcode a_opcode,
                                     SessionState a_stateOnOk)
{
    char name[64];
    char password[64];
    if (!ReadCredentials(name, sizeof(name), password, sizeof(password))) return;

    if (strlen(name) + strlen(password) + 2 > CHAT_MAX_VALUE)
    {
        printf("Credentials too long.\n");
        return;
    }

    ChatMessage req = {0};
    req.m_opcode = a_opcode;
    req.m_status = CHAT_OK;
    PackCredentials(&req, name, password);

    ChatMessage resp;
    bool closed = false;
    if (!ExchangeMessage(a_app, &req, &resp, &closed))
    {
        if (closed) a_app->m_state = SESSION_DISCONNECTED;
        return;
    }
    PrintResponse(&resp);
    if (resp.m_status == CHAT_OK) a_app->m_state = a_stateOnOk;
}

static void HandleGroupAction(ClientApp* a_app, MessageOpcode a_opcode)
{
    char groupName[128];
    if (!ReadGroupName(groupName, sizeof(groupName))) return;

    size_t len = strlen(groupName);
    if (len + 1 > CHAT_MAX_VALUE)
    {
        printf("Group name too long.\n");
        return;
    }

    ChatMessage req = {0};
    req.m_opcode = a_opcode;
    req.m_status = CHAT_OK;
    memcpy(req.m_value, groupName, len + 1);
    req.m_length = (uint16_t)(len + 1);

    ChatMessage resp;
    bool closed = false;
    if (!ExchangeMessage(a_app, &req, &resp, &closed))
    {
        if (closed) a_app->m_state = SESSION_DISCONNECTED;
        return;
    }
    PrintResponse(&resp);
}

static void HandleLogout(ClientApp* a_app)
{
    ChatMessage req = {0};
    req.m_opcode = OPCODE_LOGOUT;
    req.m_status = CHAT_OK;
    req.m_length = 0;

    ChatMessage resp;
    bool closed = false;
    if (!ExchangeMessage(a_app, &req, &resp, &closed))
    {
        if (closed) a_app->m_state = SESSION_DISCONNECTED;
        return;
    }
    PrintResponse(&resp);
    if (resp.m_status == CHAT_OK) a_app->m_state = SESSION_CONNECTED;
}

static void HandleListAction(ClientApp* a_app, MessageOpcode a_opcode)
{
    ChatMessage req = {0};
    req.m_opcode = a_opcode;
    req.m_status = CHAT_OK;
    req.m_length = 0;

    ChatMessage resp;
    bool closed = false;
    if (!ExchangeMessage(a_app, &req, &resp, &closed))
    {
        if (closed) a_app->m_state = SESSION_DISCONNECTED;
        return;
    }
    PrintResponse(&resp);
}

static void HandleExit(ClientApp* a_app)
{
    if (a_app->m_state == SESSION_DISCONNECTED) return;

    ChatMessage req = {0};
    req.m_opcode = OPCODE_EXIT;
    req.m_status = CHAT_OK;
    req.m_length = 0;

    char sendBuf[CHAT_MAX_TOTAL];
    int sentLen = SerializeChatMessage(&req, sendBuf, sizeof(sendBuf));
    if (sentLen > 0)
    {
        /* Best-effort notify; ignore errors since we're tearing down anyway. */
        (void)ClientController_Send(a_app->m_controller, sendBuf, (size_t)sentLen);
    }
    ClientController_Stop(a_app->m_controller);
    a_app->m_state = SESSION_DISCONNECTED;
}

int ClientApp_Run(ClientApp* a_app)
{
    if (a_app == NULL) return 1;

    char line[64];
    for (;;)
    {
        Menu_Render(a_app->m_state);
        if (!Menu_ReadLine("> ", line, sizeof(line)))
        {
            printf("\nEOF on stdin, exiting.\n");
            HandleExit(a_app);
            return 0;
        }

        MenuChoice choice = Menu_Parse(line, a_app->m_state);
        switch (choice)
        {
            case MENU_CONNECT:
                HandleConnect(a_app);
                break;
            case MENU_REGISTER:
                HandleCredentialedAction(a_app, OPCODE_REGISTER, SESSION_LOGGED_IN);
                break;
            case MENU_LOGIN:
                HandleCredentialedAction(a_app, OPCODE_LOGIN, SESSION_LOGGED_IN);
                break;
            case MENU_LOGOUT:
                HandleLogout(a_app);
                break;
            case MENU_CREATE_GROUP:
                HandleGroupAction(a_app, OPCODE_CREATE_GROUP);
                break;
            case MENU_JOIN_GROUP:
                HandleGroupAction(a_app, OPCODE_JOIN_GROUP);
                break;
            case MENU_LEAVE_GROUP:
                HandleGroupAction(a_app, OPCODE_LEAVE_GROUP);
                break;
            case MENU_DISPLAY_USERS:
                HandleListAction(a_app, OPCODE_LIST_USERS);
                break;
            case MENU_DISPLAY_GROUPS:
                HandleListAction(a_app, OPCODE_LIST_GROUPS);
                break;
            case MENU_EXIT:
                HandleExit(a_app);
                return 0;
            case MENU_NONE:
            default:
                printf("Invalid selection.\n");
                break;
        }
    }
}
