#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include "ServerManager.h"
#include "UserManager.h"
#include "GroupManager.h"
#include "TcpServerController.h"
#include "logger.h"
#include "config.h"
#include "NetworkProtocol.h"

struct ServerManager
{
    UserManager* m_userManager;
    GroupManager* m_groupManager;
    TcpServerController* m_tcpServerController;
};

// call back functions for TcpServerController
static void ServerManagerCallbackNewConnection(void* a_context, const TcpConnectionRecord* a_record);
static void ServerManagerCallbackDisconnect(void* a_context, const TcpConnectionRecord* a_record);
static void ServerManagerCallbackRecv(void* a_context, const TcpConnectionRecord* a_record, const char* a_message, size_t a_length);
static ServerResult ServerManager_SendMessage(const int a_fd, ChatStatus a_status, const char* a_message, size_t a_length);
static void ServerManager_SendOrLog(const TcpConnectionRecord* a_record, ChatStatus a_status, const char* a_message, size_t a_length);

static size_t ServerManagerHashFunctionDJB2(const void* a_key);
static int ServerManagerEqualFunction(const void* a_firstKey, const void* a_secondKey);

 /* Action functions */
static void ServerManager_ActionRegister(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message);
static void ServerManager_ActionLogin(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message);
static void ServerManager_ActionLogout(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message);
static void ServerManager_ActionExit(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message);
static void ServerManager_ActionCreateGroup(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message);
static void ServerManager_ActionJoinGroup(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message);
static void ServerManager_ActionLeaveGroup(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message);
static void ServerManager_ActionListUsers(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message);
static void ServerManager_ActionListGroups(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message);
 /*** End of Action functions ***/
 typedef void (*ActionFn)(ServerManager*, const TcpConnectionRecord*, const ChatMessage*);

 typedef struct
 {
    MessageOpcode m_opcode;
    const char*   m_name;
    ActionFn      m_fn;
 }ActionEntry;

 static const ActionEntry s_actions[] = 
 {
    { OPCODE_REGISTER,     "register",     ServerManager_ActionRegister     },
    { OPCODE_LOGIN,        "login",        ServerManager_ActionLogin        },
    { OPCODE_LOGOUT,       "logout",       ServerManager_ActionLogout       },
    { OPCODE_EXIT,         "exit",         ServerManager_ActionExit         },
    { OPCODE_CREATE_GROUP, "create_group", ServerManager_ActionCreateGroup  },
    { OPCODE_JOIN_GROUP,   "join_group",   ServerManager_ActionJoinGroup    },
    { OPCODE_LEAVE_GROUP,  "leave_group",  ServerManager_ActionLeaveGroup   },
    { OPCODE_LIST_USERS,   "list_users",   ServerManager_ActionListUsers    },
    { OPCODE_LIST_GROUPS,  "list_groups",  ServerManager_ActionListGroups   },
};

static const size_t s_actionsCount = sizeof(s_actions) / sizeof(s_actions[0]);


static const ActionEntry* FindAction(MessageOpcode a_opcode)
{
    for (size_t i = 0; i < s_actionsCount; ++i)
    {
        if (s_actions[i].m_opcode == a_opcode) return &s_actions[i];
    }
    return NULL;
}



/*** ServerManager functions ***/
ServerManager*
ServerManager_Create(char* a_serverName, char* a_serverIp, uint16_t a_serverPort)
{
    ServerManager* serverManager = (ServerManager*)malloc(sizeof(ServerManager));
    if (serverManager == NULL)
    {
        return NULL;
    }
    serverManager->m_userManager = UserManager_Create(ServerManagerHashFunctionDJB2, ServerManagerEqualFunction);
    if (serverManager->m_userManager == NULL)
    {
        free(serverManager);
        return NULL;
    }
    serverManager->m_groupManager = GroupManager_Create();
    if (serverManager->m_groupManager == NULL)
    {
        UserManager_Destroy(&serverManager->m_userManager);
        free(serverManager);
        return NULL;
    }
    serverManager->m_tcpServerController = TcpServerController_Create(a_serverName, a_serverIp, a_serverPort);
    if (serverManager->m_tcpServerController == NULL)
    {
        GroupManager_Destroy(&serverManager->m_groupManager);
        UserManager_Destroy(&serverManager->m_userManager);
        free(serverManager);
        return NULL;
    }

    if (TcpServerController_SetCallbacks(serverManager->m_tcpServerController,
         serverManager,
         ServerManagerCallbackNewConnection,
         ServerManagerCallbackDisconnect,
         ServerManagerCallbackRecv) != TCP_RESULT_SUCCESS)
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

ServerResult ServerManager_Stop(ServerManager* a_manager)
{
    if (a_manager == NULL)
    {
        LOG_ERROR("Server manager is NULL");
        return SERVER_RESULT_NULL_PTR;
    }
    TcpServerController_Stop(a_manager->m_tcpServerController);
    LOG_INFO("Server manager stopped");
    return SERVER_RESULT_SUCCESS;
}

// Callback functions for TcpServerController
static void
ServerManagerCallbackNewConnection(void* a_context, const TcpConnectionRecord* a_record)
{
    (void)a_context;
    LOG_INFO("New connection from %s:%d", a_record->m_ip, a_record->m_port);
}

static void
ServerManagerCallbackDisconnect(void* a_context, const TcpConnectionRecord* a_record)
{
    (void)a_context;
    LOG_INFO("Disconnection from %s:%d", a_record->m_ip, a_record->m_port);
}

static void
ServerManagerCallbackRecv(void* a_context, const TcpConnectionRecord* a_record, const char* a_message, size_t a_length)
{
    ServerManager* manager = (ServerManager*)a_context;
    LOG_INFO("message from fd=%d ip=%s: %.*s", a_record->m_fdConnection, a_record->m_ip, (int)a_length, a_message);

    ChatMessage decodedMessage;
    if (DeserializeChatMessage(a_message, a_length, &decodedMessage) != CHAT_OK)
    {
        LOG_ERROR("Failed to deserialize message");
        ServerManager_SendOrLog(a_record, CHAT_ERR_MALFORMED, NULL, 0);
        return;
    }

    const ActionEntry* action = FindAction(decodedMessage.m_opcode);
    if (action == NULL)
    {
        LOG_WARN("Unhandled opcode: 0x%02x", decodedMessage.m_opcode);
        ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC, NULL, 0);
        return;
    }
    LOG_DEBUG("Dispatching action: %s", action->m_name);
    action->m_fn(manager, a_record, &decodedMessage);
}

static void ServerManager_ActionRegister(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    LOG_INFO("Registering user: %s", a_message->m_value);

    const char* username = (const char*)a_message->m_value;
    const char* password = username + strlen(username) + 1;
    UserManagerResult addUserResult = UserManager_AddUser(a_manager->m_userManager, a_record->m_fdConnection, username, password);
    if (addUserResult != USER_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("(TODO: Handle this error) Failed to add user: %s", UserManagerResult_ToString(addUserResult));
        ServerManager_SendOrLog(a_record, CHAT_ERR_BAD_CREDS, UserManagerResult_ToString(addUserResult), a_message->m_length);
    }
    ServerManager_SendOrLog(a_record, CHAT_OK, "User added", sizeof("User added"));
}

static void ActionNotImplemented(const TcpConnectionRecord* a_record, const char* a_name)
{
    LOG_INFO("Action '%s' not yet implemented", a_name);
    ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC, "Not implemented", sizeof("Not implemented"));
}

static void ServerManager_ActionLogin(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_manager; (void)a_message;
    ActionNotImplemented(a_record, "login");
}

static void ServerManager_ActionLogout(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_manager; (void)a_message;
    ActionNotImplemented(a_record, "logout");
}

static void ServerManager_ActionExit(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_manager; (void)a_message;
    ActionNotImplemented(a_record, "exit");
}

static void ServerManager_ActionCreateGroup(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_manager; (void)a_message;
    ActionNotImplemented(a_record, "create_group");
}

static void ServerManager_ActionJoinGroup(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_manager; (void)a_message;
    ActionNotImplemented(a_record, "join_group");
}

static void ServerManager_ActionLeaveGroup(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_manager; (void)a_message;
    ActionNotImplemented(a_record, "leave_group");
}

static void ServerManager_ActionListUsers(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_manager; (void)a_message;
    ActionNotImplemented(a_record, "list_users");
}

static void ServerManager_ActionListGroups(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_manager; (void)a_message;
    ActionNotImplemented(a_record, "list_groups");
}

const char*
ServerResult_ToString(const ServerResult a_result)
{
    switch ((int)a_result)
    {
        case SERVER_RESULT_SUCCESS: return "SUCCESS";
        case SERVER_RESULT_NULL_PTR: return "NULL_PTR";
        case SERVER_RESULT_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case SERVER_RESULT_ALLOCATION_FAILED: return "ALLOCATION_FAILED";
        case SERVER_RESULT_ALREADY_RUNNING: return "ALREADY_RUNNING";
        case SERVER_RESULT_NOT_RUNNING: return "NOT_RUNNING";
        case SERVER_RESULT_NETWORK_ERROR: return "NETWORK_ERROR";
        case SERVER_RESULT_SEND_ERROR: return "SEND_ERROR";
        case SERVER_RESULT_RECEIVE_ERROR: return "RECEIVE_ERROR";
        case SERVER_RESULT_INTERNAL_ERROR: return "INTERNAL_ERROR";
        default: return "UNKNOWN";
    }


}

static ServerResult 
ServerManager_SendMessage(const int a_fd, ChatStatus a_status, const char* a_message, size_t a_length)
{
    if (a_length > CHAT_MAX_VALUE)
    {
        LOG_ERROR("ServerManager_SendMessage: message too long");
        return SERVER_RESULT_INVALID_ARGUMENT;
    }
    if (a_length > 0 && a_message == NULL)
    {
        LOG_ERROR("ServerManager_SendMessage: NULL message with non-zero length");
        return SERVER_RESULT_INVALID_ARGUMENT;
    }

    ChatMessage encodedMessage;
    encodedMessage.m_opcode = OPCODE_RESPONSE;
    encodedMessage.m_status = a_status;
    encodedMessage.m_length = (uint16_t)a_length;
    if (a_length > 0)
    {
        memcpy(encodedMessage.m_value, a_message, a_length);
    }

    size_t buf_size = (size_t)CHAT_HEADER_SIZE + a_length;
    char* serializedMessage = (char*)malloc(buf_size);
    if (serializedMessage == NULL)
    {
        LOG_ERROR("ServerManager_SendMessage: failed to allocate memory");
        return SERVER_RESULT_ALLOCATION_FAILED;
    }

    int serialized_len = SerializeChatMessage(&encodedMessage, serializedMessage, buf_size);
    if (serialized_len < 0)
    {
        LOG_ERROR("ServerManager_SendMessage: failed to serialize message");
        free(serializedMessage);
        return SERVER_RESULT_INTERNAL_ERROR;
    }

    ssize_t bytes_sent = send(a_fd, serializedMessage, (size_t)serialized_len, 0);
    free(serializedMessage);
    if (bytes_sent < 0)
    {
        LOG_ERROR("ServerManager_SendMessage: failed to send message");
        return SERVER_RESULT_SEND_ERROR;
    }
    if (bytes_sent != serialized_len)
    {
        LOG_ERROR("ServerManager_SendMessage: partial send");
        return SERVER_RESULT_SEND_ERROR;
    }
    return SERVER_RESULT_SUCCESS;
}

static void
ServerManager_SendOrLog(const TcpConnectionRecord* a_record, ChatStatus a_status, const char* a_message, size_t a_length)
{
    ServerResult sendResult = ServerManager_SendMessage(
        a_record->m_fdConnection, a_status, a_message, a_length);

    if (sendResult != SERVER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to send message to %s:%d: %s",
                  a_record->m_ip, a_record->m_port,
                  ServerResult_ToString(sendResult));
    }
}

// Hash function for UserManager and GroupManager
static size_t
ServerManagerHashFunctionDJB2(const void *a_str) {
    unsigned long hash = 5381;
    int c;

    while ((c = *(const char*)a_str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash;
}

static int 
ServerManagerEqualFunction(const void* a_firstKey, const void* a_secondKey)
{
    return strcmp((const char*)a_firstKey, (const char*)a_secondKey) == 0;
}