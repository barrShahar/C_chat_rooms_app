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
#include "User.h"
#include "network_utils.h"

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
static void ServerManager_LeaveAllGroups(ServerManager* a_manager, int a_fdConnection);
static void ServerManager_CleanupSession(ServerManager* a_manager, const TcpConnectionRecord* a_record);
static void ServerManager_SendGroupEndpoint(ServerManager* a_manager, const TcpConnectionRecord* a_record, const char* a_groupName);

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
    serverManager->m_groupManager = GroupManager_Create(ServerManagerHashFunctionDJB2, ServerManagerEqualFunction);
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
    ServerManager* manager = (ServerManager*)a_context;
    LOG_INFO("Disconnection from %s:%d", a_record->m_ip, a_record->m_port);
    ServerManager_CleanupSession(manager, a_record);
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
        LOG_ERROR("(TODO: Handle this error) Failed to add user: %s", UserManagerResult_toString(addUserResult));
        ServerManager_SendOrLog(a_record, CHAT_ERR_BAD_CREDS, UserManagerResult_toString(addUserResult), strlen(UserManagerResult_toString(addUserResult)) + 1);
        return;
    }
    ServerManager_SendOrLog(a_record, CHAT_OK, NULL, 0);
}

static void ServerManager_ActionLogin(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    const char* username = (const char*)a_message->m_value;
    const char* password = username + strlen(username) + 1;
    UserManagerResult loginResult = UserManager_Login(a_manager->m_userManager, a_record->m_fdConnection, username, password);
    if (loginResult == USER_MANAGER_RESULT_ALREADY_LOG)
    {
        LOG_ERROR("User already logged in: %s", username);
        ServerManager_SendOrLog(a_record, CHAT_ERR_ALREADY_LOGGED_IN, NULL, 0);
        return;
    }
    if (loginResult != USER_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to login: %s", UserManagerResult_toString(loginResult));
        ServerManager_SendOrLog(a_record, CHAT_ERR_BAD_CREDS, UserManagerResult_toString(loginResult), a_message->m_length);
        return;
    }
    LOG_INFO("Login successful for user: %s", username);
    ServerManager_SendOrLog(a_record, CHAT_OK, "Login successful", sizeof("Login successful"));
    return;
}

static void 
ServerManager_ActionLogout(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_message;

    LOG_DEBUG("Logging out user: %d", a_record->m_fdConnection);

    // Validate user is logged in
    bool isLoggedIn = false;
    UserManager_IsUserLoggedIn(a_manager->m_userManager, NULL, a_record->m_fdConnection, &isLoggedIn);
    if (isLoggedIn == false)
    {
        SOFT_ASSERT(false);
        ServerManager_SendOrLog(a_record, CHAT_ERR_NOT_LOGGED_IN, NULL, 0);
        return;
    }

    // 1. & 2. leave all groups (decrease ref count, remove if empty)
    ServerManager_LeaveAllGroups(a_manager, a_record->m_fdConnection);

    // 3. logout user
    char* username; // for logging
    UserManagerResult logoutResult = UserManager_Logout(a_manager->m_userManager, a_record->m_fdConnection, &username);
    if (logoutResult != USER_MANAGER_RESULT_SUCCESS)
    {
        SOFT_ASSERT(false);
        ServerManager_SendOrLog(a_record, CHAT_ERR_BAD_CREDS, UserManagerResult_toString(logoutResult), strlen(UserManagerResult_toString(logoutResult))+1);
        return;
    }

    username = username == NULL ? strdup(a_record->m_ip): username;
    LOG_INFO("Logout successful for user: %s", username);
    ServerManager_SendOrLog(a_record, CHAT_OK, "Logout successful", sizeof("Logout successful"));
    free(username);
    return;
}

/* Decrease the ref count of every group the user belongs to and remove any that
 * become empty. Safe to call for a user that is in no groups. Shared by logout
 * and exit. */
static void
ServerManager_LeaveAllGroups(ServerManager* a_manager, int a_fdConnection)
{
    size_t userGroupCount = 0;
    UserManager_GetUserGroupCount(a_manager->m_userManager, a_fdConnection, &userGroupCount);
    if (userGroupCount == 0)
    {
        return;
    }

    char* userGroups[userGroupCount];
    size_t numberOfGroupsWritten = 0;
    UserManager_GetUserGroups(a_manager->m_userManager,
        a_fdConnection,
        (const char**)userGroups,
        userGroupCount,
        &numberOfGroupsWritten);

    SOFT_ASSERT(numberOfGroupsWritten == userGroupCount);
    for (size_t i = 0; i < userGroupCount; i++)
    {
        GroupManager_DecreaseGroupRefCount(a_manager->m_groupManager, userGroups[i], NULL);
        GroupManagerResult removeResult = GroupManager_RemoveGroupIfEmpty(a_manager->m_groupManager, userGroups[i]);
        if (removeResult == GROUP_MANAGER_RESULT_SUCCESS)
        {
            LOG_INFO("Group %s removed", userGroups[i]);
        }
    }
}

/* Tear down a connection's session: leave all groups and log the user out. A
 * client may exit/disconnect without ever logging in, so the absence of a
 * session is tolerated. Does not touch the socket and does not reply, so it is
 * safe to call from the disconnect callback where the peer is already gone. */
static void
ServerManager_CleanupSession(ServerManager* a_manager, const TcpConnectionRecord* a_record)
{
    bool isLoggedIn = false;
    UserManager_IsUserLoggedIn(a_manager->m_userManager, NULL, a_record->m_fdConnection, &isLoggedIn);
    if (isLoggedIn == false)
    {
        return;
    }

    ServerManager_LeaveAllGroups(a_manager, a_record->m_fdConnection);

    char* username = NULL;
    UserManagerResult logoutResult =
        UserManager_Logout(a_manager->m_userManager, a_record->m_fdConnection, &username);
    if (logoutResult != USER_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("Session cleanup: failed to logout fd %d: %s",
            a_record->m_fdConnection, UserManagerResult_toString(logoutResult));
    }
    else
    {
        LOG_INFO("Session cleanup: logged out user %s", username != NULL ? username : a_record->m_ip);
    }
    free(username);
}

static void
ServerManager_ActionExit(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_message;

    LOG_DEBUG("Exit requested by fd: %d", a_record->m_fdConnection);

    /* The socket teardown is driven by the client closing the connection, which
     * subsequently fires ServerManagerCallbackDisconnect. */
    ServerManager_CleanupSession(a_manager, a_record);
    ServerManager_SendOrLog(a_record, CHAT_OK, "Goodbye", sizeof("Goodbye"));
}


/* Reply CHAT_OK with the group's multicast endpoint formatted as "ip:port".
 * The client uses this to join the group's UDP multicast channel. */
static void
ServerManager_SendGroupEndpoint(ServerManager* a_manager, const TcpConnectionRecord* a_record, const char* a_groupName)
{
    GroupEndpoint ep;
    GroupManagerResult result = GroupManager_GetGroupEndpoint(a_manager->m_groupManager, a_groupName, &ep);
    if (result != GROUP_MANAGER_RESULT_SUCCESS)
    {
        /* The group was just created/joined, so a missing endpoint is a bug. */
        SOFT_ASSERT(false);
        LOG_ERROR("Failed to get group endpoint: %s", GroupManagerResult_toString(result));
        ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC,
            GroupManagerResult_toString(result), strlen(GroupManagerResult_toString(result)) + 1);
        return;
    }

    char ipbuf[INET_ADDRSTRLEN];
    char endpoint[CONF_MULTICAST_ENDPOINT_STR_MAX];
    network_convert_ip_n_to_p(ep.m_multicastAddr, ipbuf);
    int n = snprintf(endpoint, sizeof(endpoint), "%s:%u", ipbuf, (unsigned)ep.m_port);
    if (n < 0 || (size_t)n >= sizeof(endpoint))
    {
        LOG_ERROR("Failed to format group endpoint");
        ServerManager_SendOrLog(a_record, CHAT_OK, NULL, 0);
        return;
    }
    ServerManager_SendOrLog(a_record, CHAT_OK, endpoint, (size_t)n + 1);
}

static void ServerManager_ActionCreateGroup(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    // Create group
    char* groupName = (char*)a_message->m_value;
    GroupManagerResult addGroupResult = GroupManager_AddGroup(a_manager->m_groupManager, groupName);
    if (addGroupResult != GROUP_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to add group: %s", GroupManagerResult_toString(addGroupResult));
        ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC, GroupManagerResult_toString(addGroupResult), strlen(GroupManagerResult_toString(addGroupResult)));
        return;
    }
    // Add group name to the user who created it
    UserManagerResult result = UserManager_AddUserToGroup(a_manager->m_userManager, a_record->m_fdConnection, groupName);
    if (result != USER_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to add group to user: %s", UserManagerResult_toString(result));
        GroupManagerResult removeGroupResult = GroupManager_RemoveGroupIfEmpty(a_manager->m_groupManager, groupName);
        if (removeGroupResult != GROUP_MANAGER_RESULT_SUCCESS)
        {
            LOG_ERROR("Failed to remove an emptygroup: %s", GroupManagerResult_toString(removeGroupResult));
        }
        // Send error message to the user
        ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC, UserManagerResult_toString(result), strlen(UserManagerResult_toString(result)) + 1);
        // Remove group from the group manager
        GroupManager_RemoveGroupIfEmpty(a_manager->m_groupManager, groupName);
        return;
    }

    // Increase group ref count
    GroupManager_IncreaseGroupRefCount(a_manager->m_groupManager, groupName, NULL);
    LOG_INFO("Group created: %s", groupName);

    // Return the group's multicast endpoint to the creator
    ServerManager_SendGroupEndpoint(a_manager, a_record, groupName);
}

static void ServerManager_ActionJoinGroup(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    char* groupName = (char*)a_message->m_value;
    // 1. Get group by name
    Group* group = NULL;
    GroupManagerResult getGroupResult = GroupManager_GetGroup(a_manager->m_groupManager, groupName, &group);
    if (getGroupResult != GROUP_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to get group: %s", GroupManagerResult_toString(getGroupResult));
        ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC, GroupManagerResult_toString(getGroupResult), strlen(GroupManagerResult_toString(getGroupResult)) + 1);
        return;
    }
    // 2. Check if user exist and log in
    bool isLoggedIn;
    UserManager_IsUserLoggedIn(a_manager->m_userManager, NULL, a_record->m_fdConnection, &isLoggedIn);
    if (isLoggedIn == false)
    {
        LOG_ERROR("User is not logged in");
        ServerManager_SendOrLog(a_record, CHAT_ERR_NOT_LOGGED_IN, NULL, 0);
        return;
    }
    // 3. Add Group to User's groups
    UserManagerResult result =
        UserManager_AddUserToGroup(a_manager->m_userManager, a_record->m_fdConnection, groupName);
    
    if (result == USER_MANAGER_RESULT_SUCCESS)
    {
        GroupManagerResult incResult =
            GroupManager_IncreaseGroupRefCount(a_manager->m_groupManager, groupName, NULL);
        if (incResult != GROUP_MANAGER_RESULT_SUCCESS)
        {
            LOG_ERROR("Failed to increase group ref count: %s", GroupManagerResult_toString(incResult));
            ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC, GroupManagerResult_toString(incResult),
                                     strlen(GroupManagerResult_toString(incResult)) + 1);
            return;
        }
        LOG_DEBUG("Group %s ref count is %zu", groupName, Group_GetRefCount(group));
        ServerManager_SendGroupEndpoint(a_manager, a_record, groupName);
        return;
    }

    LOG_ERROR("Failed to add user to group: %s", UserManagerResult_toString(result));
    ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC, UserManagerResult_toString(result), strlen(UserManagerResult_toString(result)) + 1);
    return;
}

static void ServerManager_ActionLeaveGroup(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{

    // 1. Get group name and validate it
    char* groupName = (char*)a_message->m_value;
    // Check if group exists
    Group* group = NULL;
    GroupManagerResult getGroupResult = GroupManager_GetGroup(a_manager->m_groupManager, groupName, &group);
    if (getGroupResult != GROUP_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to get group: %s", GroupManagerResult_toString(getGroupResult));
        ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC, GroupManagerResult_toString(getGroupResult), strlen(GroupManagerResult_toString(getGroupResult)) + 1);
        return;
    }
    // Check if user is logged in
    bool isLoggedIn;
    UserManager_IsUserLoggedIn(a_manager->m_userManager, NULL, a_record->m_fdConnection, &isLoggedIn);
    if (isLoggedIn == false)
    {
        LOG_ERROR("User is not logged in");
        ServerManager_SendOrLog(a_record, CHAT_ERR_NOT_LOGGED_IN, NULL, 0);
        return;
    }
    // Check if user is in group
    bool isInGroup;
    UserManagerResult result = UserManager_IsUserInGroup(a_manager->m_userManager, a_record->m_fdConnection, groupName, &isInGroup);
    if (result != USER_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to check if user is in group: %s", UserManagerResult_toString(result));
        ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC, UserManagerResult_toString(result), strlen(UserManagerResult_toString(result)) + 1);
        return;
    }
    if (isInGroup == false)
    {
        LOG_ERROR("User is not in group");
        ServerManager_SendOrLog(a_record, CHAT_ERR_NOT_IN_GROUP, NULL, 0);
        return;
    }

    // 2. Remove Group from User's groups
    UserManagerResult removeResult = UserManager_RemoveUserFromGroup(a_manager->m_userManager, a_record->m_fdConnection, groupName);
    if (removeResult != USER_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to remove group from user: %s", UserManagerResult_toString(removeResult));
        ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC, UserManagerResult_toString(removeResult), strlen(UserManagerResult_toString(removeResult)) + 1);
        return;
    }
    // 3. Decrease Group ref count
    GroupManagerResult decreaseResult = GroupManager_DecreaseGroupRefCount(a_manager->m_groupManager, groupName, NULL);
    if (decreaseResult != GROUP_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to decrease group ref count: %s", GroupManagerResult_toString(decreaseResult));
        ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC, GroupManagerResult_toString(decreaseResult), strlen(GroupManagerResult_toString(decreaseResult)) + 1);
        return;
    }
    GroupManager_RemoveGroupIfEmpty(a_manager->m_groupManager, groupName);
    LOG_INFO("User left Group %s", groupName);
    ServerManager_SendOrLog(a_record, CHAT_OK, NULL, 0);
    return;
}

static void ServerManager_ActionListUsers(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_message;

    /* Log the full dump server-side for monitoring. */
    UserManager_GetAllUsersAndTheirGroups(a_manager->m_userManager);

    /* UserManager_FormatAllUsersAndGroups truncates to fit the buffer, so a
     * wire-sized buffer is always safe and never exceeds CHAT_MAX_VALUE. This
     * also handles the empty case (no users -> empty string). */
    char bufferUsers[CHAT_MAX_VALUE];
    UserManagerResult formatResult = UserManager_FormatAllUsersAndGroups(
        a_manager->m_userManager, bufferUsers, sizeof(bufferUsers));
    if (formatResult != USER_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to format user list: %s", UserManagerResult_toString(formatResult));
        ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC,
            "Failed to list users", strlen("Failed to list users") + 1);
        return;
    }

    /* Send the actual string length (including the null terminator), not the
     * buffer capacity, so the client renders exactly the lines produced. */
    ServerManager_SendOrLog(a_record, CHAT_OK,
        bufferUsers, strlen(bufferUsers) + 1);
}

static void ServerManager_ActionListGroups(ServerManager* a_manager, const TcpConnectionRecord* a_record, const ChatMessage* a_message)
{
    (void)a_message;

    /* GroupManager_FormatGroupList truncates to fit the buffer, so a wire-sized
     * buffer is always safe and never exceeds CHAT_MAX_VALUE. This also handles
     * the empty-list case (no groups -> empty string). */
    char bufferGroupNames[CHAT_MAX_VALUE];
    GroupManagerResult formatResult = GroupManager_FormatGroupList(
        a_manager->m_groupManager, bufferGroupNames, sizeof(bufferGroupNames));
    if (formatResult != GROUP_MANAGER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to format group list");
        ServerManager_SendOrLog(a_record, CHAT_ERR_GENERIC,
            "Failed to list groups", strlen("Failed to list groups") + 1);
        return;
    }

    /* Send the actual string length (including the null terminator), not the
     * buffer capacity, so the client renders exactly the names produced. */
    ServerManager_SendOrLog(a_record, CHAT_OK,
        bufferGroupNames, strlen(bufferGroupNames) + 1);
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