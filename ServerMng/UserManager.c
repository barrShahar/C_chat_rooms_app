#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "UserManager.h"
#include "logger.h"
#include "HashMap.h"
#include "User.h"
#include "network_utils.h"
#include "config.h"

struct UserManager
{
    HashMap* m_usersByName;
    HashMap* m_usersByFd;
    size_t (*m_hashFunction)(const void* _key);
    int (*m_equalFunction)(const void* _firstKey, const void* _secondKey);
};

static size_t UserManagerHashFunctionFd(const void* a_key);
static int UserManagerEqualFunctionFd(const void* a_firstKey, const void* a_secondKey);
static UserManagerResult FormatUserGroupsLine(const User* a_user, char* a_buf, size_t a_bufSize);
static int PrintUserGroupsLine(const void* a_key, void* a_value, void* a_context);
static int AppendUserGroupsLine(const void* a_key, void* a_value, void* a_context);

typedef struct FormatAllUsersContext
{
    char* m_buf;
    size_t m_bufSize;
    size_t m_offset;
    bool m_first;
} FormatAllUsersContext;

UserManager* UserManager_Create(
    size_t (*a_hashFunction)(const void* _key),
    int (*a_equalFunction)(const void* _firstKey, const void* _secondKey))
{
    UserManager* userManager = (UserManager*)malloc(sizeof(UserManager));
    if (userManager == NULL)
    {
        LOG_ERROR("Failed to allocate memory for user manager");
        return NULL;
    }
    userManager->m_usersByName = HashMap_Create(10, a_hashFunction, a_equalFunction);
    if (userManager->m_usersByName == NULL)
    {
        LOG_ERROR("Failed to create hash map for users");
        free(userManager);
        return NULL;
    }
    userManager->m_usersByFd = HashMap_Create(10, UserManagerHashFunctionFd, UserManagerEqualFunctionFd);
    if (userManager->m_usersByFd == NULL)
    {
        LOG_ERROR("Failed to create hash map for users by fd");
        HashMap_Destroy(&userManager->m_usersByName, NULL, NULL);
        free(userManager);
        return NULL;
    }
    userManager->m_hashFunction = a_hashFunction;
    userManager->m_equalFunction = a_equalFunction;
    
    LOG_INFO("User manager created");
    return userManager;
}

UserManagerResult 
UserManager_AddUserToGroup(UserManager* a_manager, const int a_fdConnection, const char* a_groupName)
{
    // 1. parameters check
    if (a_manager == NULL || a_groupName == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }

    // 2. get user by fd
    User* user;
    HashMap_Find(a_manager->m_usersByFd, &a_fdConnection, (void**)&user);
    if (user == NULL)
    {
        LOG_ERROR("User not found");
        return USER_MANAGER_RESULT_NOT_FOUND;
    }
    // 3. check if user is logged in
    if (User_GetState(user) == USER_STATE_OFFLINE)
    {
        LOG_ERROR("User is offline");
        return USER_MANAGER_RESULT_NOT_LOGGED;
    }

    UserResult addGroupResult = User_AddGroup(user, a_groupName);
    if (addGroupResult == USER_RESULT_ALREADY_IN_GROUP)
    {
        LOG_DEBUG("User is already in the group");
        return USER_MANAGER_RESULT_ALREADY_IN_GROUP;
    }
    if (addGroupResult != USER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to add group to user");
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }
    return USER_MANAGER_RESULT_SUCCESS;
}


UserManagerResult 
UserManager_RemoveUserFromGroup(UserManager* a_manager, const int a_fdConnection, const char* a_groupName)
{
    // 1. parameters check
    if (a_manager == NULL || a_groupName == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }

    // 2. Get user by fd
    User* user;
    HashMap_Find(a_manager->m_usersByFd, &a_fdConnection, (void**)&user);
    if (user == NULL)
    {
        LOG_ERROR("User not found");
        return USER_MANAGER_RESULT_NOT_FOUND;
    }

    // 3. Remove group from user if group exists
    UserResult removeGroupResult = User_RemoveGroup(user, a_groupName);
    if (removeGroupResult == USER_RESULT_NOT_IN_GROUP)
    {
        LOG_DEBUG("User is not in the group");
        return USER_MANAGER_RESULT_NOT_IN_GROUP;
    }
    if (removeGroupResult != USER_RESULT_SUCCESS)
    {
        LOG_ERROR("Failed to remove group from user");
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }
   
    return USER_MANAGER_RESULT_SUCCESS;
}

void 
UserManager_Destroy(UserManager** a_manager)
{
    if (a_manager == NULL || *a_manager == NULL)
    {
        return;
    }

    HashMap_Destroy(&(*a_manager)->m_usersByName, NULL, NULL);
    HashMap_Destroy(&(*a_manager)->m_usersByFd, NULL, NULL);
    free(*a_manager);
    *a_manager = NULL;
}


UserManagerResult 
UserManager_AddUser(UserManager* a_manager, const int a_fdConnection, const char* a_username, const char* a_password)
{
    // 1. parameters check
    if (a_manager == NULL || a_username == NULL || a_password == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }
    LOG_DEBUG("Adding user: %s, password: %s", a_username, a_password);

    // 2. create user
    User* user = User_Create(a_fdConnection, a_username, a_password);
    if (user == NULL)
    {
        return USER_MANAGER_RESULT_ALLOCATION_FAILED;
    }

    // 3. insert user into hash map by name
    const char* username = User_GetUsername(user); // User class takes ownership of the username ptr
    MapResult insertResult = HashMap_Insert(a_manager->m_usersByName, username, user);
    if (insertResult == MAP_KEY_DUPLICATE_ERROR)
    {
        LOG_ERROR("User name is already taken");
        User_Destroy(&user);
        return USER_MANAGER_RESULT_NAME_TAKEN;
    }
    if (insertResult != MAP_SUCCESS)
    {
        LOG_ERROR("Failed to insert user into hash map by name");
        User_Destroy(&user);
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }

    
    // User class takes ownership of the fdConnection
    const int* fdConnection = User_GetFdConnection(user); 
    if (fdConnection == NULL)
    {
        LOG_ERROR("Failed to get fd connection");
        SOFT_ASSERT(fdConnection != NULL);
        User_Destroy(&user);
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }


    // 4. insert user into hash map by fd
    insertResult = HashMap_Insert(a_manager->m_usersByFd, fdConnection, user);
    if (insertResult != MAP_SUCCESS)
    {
        LOG_ERROR("Failed to insert user into hash map by fd");
        HashMap_Remove(a_manager->m_usersByName, a_username, NULL, NULL);
        User_Destroy(&user);
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }

    // 5. set user state to online
    User_SetState(user, USER_STATE_ONLINE);
    LOG_INFO("User added: %s", a_username);
    return USER_MANAGER_RESULT_SUCCESS;
}

UserManagerResult 
UserManager_RemoveUser(UserManager* a_manager, const int a_fdConnection)
{
    if (a_manager == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }

    User* user;
    HashMap_Find(a_manager->m_usersByFd, &a_fdConnection, (void**)&user);
    if (user == NULL)
    {
        LOG_ERROR("Looged User not found!!!");
        return USER_MANAGER_RESULT_NOT_FOUND;
    }
    HashMap_Remove(a_manager->m_usersByName, User_GetUsername(user), NULL, NULL);
    HashMap_Remove(a_manager->m_usersByFd, &a_fdConnection, NULL, NULL);
    User_Destroy(&user);
    return USER_MANAGER_RESULT_SUCCESS;
}

UserManagerResult 
UserManager_Login(UserManager* a_manager, const int a_fdConnection, const char* a_username, const char* a_password)
{
    if (a_manager == NULL || a_username == NULL || a_password == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }
    User* user;

    HashMap_Find(a_manager->m_usersByName, a_username, (void**)&user);
    if (user == NULL)
    {
        return USER_MANAGER_RESULT_NOT_FOUND;
    }   
    if (User_GetState(user) == USER_STATE_ONLINE)
    {
        return USER_MANAGER_RESULT_ALREADY_LOG;
    }
    if (strcmp(User_GetPassword(user), a_password) != 0)
    {
        return USER_MANAGER_RESULT_BAD_CREDS;
    }
    if (User_SetFdConnection(user, a_fdConnection) != USER_RESULT_SUCCESS)
    {
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }
    const int* fdConnection = User_GetFdConnection(user);
    if (fdConnection == NULL)
    {
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }
    MapResult insertResult = HashMap_Insert(a_manager->m_usersByFd, fdConnection, user);
    if (insertResult != MAP_SUCCESS)
    {
        LOG_ERROR("Failed to insert user into hash map by fd");
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }

    if (User_SetState(user, USER_STATE_ONLINE) != USER_RESULT_SUCCESS)
    {
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }
    return USER_MANAGER_RESULT_SUCCESS;
}


UserManagerResult 
UserManager_Logout(UserManager* a_manager, const int a_fdConnection, char** a_loggedOutUsername)
{
    if (a_manager == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }
    User* user;

    HashMap_Find(a_manager->m_usersByFd, &a_fdConnection, (void**)&user);
    if (user == NULL)
    {
        return USER_MANAGER_RESULT_NOT_FOUND;
    }
    if (User_GetState(user) == USER_STATE_OFFLINE)
    {
        return USER_MANAGER_RESULT_NOT_LOGGED;
    }

    // Destroy Groups strings 
    User_DestroyGroups(user);

    if (User_SetState(user, USER_STATE_OFFLINE) != USER_RESULT_SUCCESS)
    {
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }
    HashMap_Remove(a_manager->m_usersByFd, &a_fdConnection, NULL, NULL);
    if (a_loggedOutUsername != NULL)
    {
        *a_loggedOutUsername = networkCopyString(User_GetUsername(user));
    }
    User_SetFdConnection(user, -1);
    return USER_MANAGER_RESULT_SUCCESS;
}

UserManagerResult 
UserManager_IsUserLoggedIn(const UserManager* a_manager, 
                           const char* a_username, 
                           const int a_fdConnection, 
                           bool* a_isLoggedIn)
{
    if (a_manager == NULL || a_isLoggedIn == NULL)
    {
        LOG_ERROR("Null pointer");
        return USER_MANAGER_RESULT_NULL_PTR;
    }
    User* user;

    // Find by name
    if (a_username != NULL)
    {
        HashMap_Find(a_manager->m_usersByName, a_username, (void**)&user);
        const int* fdConnection = (user != NULL) ? User_GetFdConnection(user) : NULL;
        if (fdConnection != NULL && *fdConnection == a_fdConnection)
        {
            *a_isLoggedIn = true;
            return USER_MANAGER_RESULT_SUCCESS;
        }
        *a_isLoggedIn = false;
        return USER_MANAGER_RESULT_SUCCESS;   
    }
    
    // Else, find by fd. Presence in m_usersByFd means the user is logged in:
    // Login inserts the user there and Logout removes them.
    user = NULL;
    HashMap_Find(a_manager->m_usersByFd, &a_fdConnection, (void**)&user);
    *a_isLoggedIn = (user != NULL);
    return USER_MANAGER_RESULT_SUCCESS;

}

UserManagerResult
UserManager_IsUserInGroup(UserManager* a_manager, const int a_fdConnection, const char* a_groupName, bool* a_isInGroup)
{
    // 1. parameters check
    if (a_manager == NULL || a_groupName == NULL || a_isInGroup == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }

    // 2. get user by fd
    User* user;
    HashMap_Find(a_manager->m_usersByFd, &a_fdConnection, (void**)&user);
    if (user == NULL)
    {
        return USER_MANAGER_RESULT_NOT_FOUND;
    }

    // 3. check if user is in group
    *a_isInGroup = User_IsInGroup(user, a_groupName) != NULL;
    return USER_MANAGER_RESULT_SUCCESS;
}

UserManagerResult
UserManager_GetAllUsersAndTheirGroups(UserManager* a_manager)
{
    if (a_manager == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }

    LOG_INFO("=== Users and their groups ===");
    HashMap_ForEach(a_manager->m_usersByName, PrintUserGroupsLine, NULL);
    return USER_MANAGER_RESULT_SUCCESS;
}

/**
 * @brief Format a single user's groups line into a_buf.
 * @details Writes "<username> -> g1, g2, ..." or "<username> -> (none)".
 *          Pure formatter (no logging/IO) so it can be reused for client
 *          responses. Truncates cleanly on buffer overflow.
 */
static UserManagerResult
FormatUserGroupsLine(const User* a_user, char* a_buf, size_t a_bufSize)
{
    if (a_user == NULL || a_buf == NULL || a_bufSize == 0)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }

    a_buf[0] = '\0';
    size_t offset = 0;
    const char* username = User_GetUsername(a_user);
    if (username == NULL)
    {
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }
    int written = snprintf(a_buf, a_bufSize, "%s -> ", username);
    if (written < 0 || (size_t)written >= a_bufSize)
    {
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }
    offset += (size_t)written;

    List* groups = NULL;
    if (User_GetGroups((User*)a_user, &groups) != USER_RESULT_SUCCESS)
    {
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }
    if (groups == NULL || ListSize(groups) == 0)
    {
        snprintf(a_buf + offset, a_bufSize - offset, "(none)");
        return USER_MANAGER_RESULT_SUCCESS;
    }

    size_t i = 0;
    for (ListItr itr = ListItrBegin(groups); itr != ListItrEnd(groups); itr = ListItrNext(itr))
    {
        const char* groupName = (const char*)ListItrGet(itr);
        if (groupName == NULL)
        {
            continue;
        }

        written = snprintf(a_buf + offset,
                           a_bufSize - offset,
                           (i == 0) ? "%s" : ", %s",
                           groupName);
        if (written < 0)
        {
            return USER_MANAGER_RESULT_INTERNAL_ERROR;
        }
        if ((size_t)written >= a_bufSize - offset)
        {
            LOG_WARN("User groups line truncated for user %s", username);
            break;
        }
        offset += (size_t)written;
        ++i;
    }

    return USER_MANAGER_RESULT_SUCCESS;
}

static int
PrintUserGroupsLine(const void* a_key, void* a_value, void* a_context)
{
    (void)a_key;
    (void)a_context;
    const User* user = (const User*)a_value;
    char line[CONF_USER_GROUPS_LINE_BUF_SIZE];

    if (FormatUserGroupsLine(user, line, sizeof(line)) == USER_MANAGER_RESULT_SUCCESS)
    {
        LOG_INFO("%s", line);
    }
    return 1;
}

UserManagerResult
UserManager_GetUserGroupCount(UserManager* a_manager, const int a_fdConnection,
                              size_t* a_outCount)
{
    // 1. parameters check
    if (a_manager == NULL || a_outCount == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }
    *a_outCount = 0;

    // 2. get user by fd
    User* user;
    HashMap_Find(a_manager->m_usersByFd, &a_fdConnection, (void**)&user);
    if (user == NULL)
    {
        return USER_MANAGER_RESULT_NOT_FOUND;
    }

    // 3. count the user's groups
    List* groups = NULL;
    User_GetGroups(user, &groups);
    if (groups == NULL)
    {
        return USER_MANAGER_RESULT_SUCCESS; // No groups, count is 0
    }

    *a_outCount = ListSize(groups);
    return USER_MANAGER_RESULT_SUCCESS;
}

UserManagerResult
UserManager_GetUserGroups(UserManager* a_manager, const int a_fdConnection,
                          const char** a_outNames, size_t a_capacity,
                          size_t* a_outCount)
{
    // 1. parameters check
    if (a_manager == NULL || a_outNames == NULL || a_outCount == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }
    *a_outCount = 0;

    // 2. get user by fd
    User* user;
    HashMap_Find(a_manager->m_usersByFd, &a_fdConnection, (void**)&user);
    if (user == NULL)
    {
        return USER_MANAGER_RESULT_NOT_FOUND;
    }

    // 3. get groups
    List* groups = NULL;
    User_GetGroups(user, &groups);
    if (groups == NULL)
    {
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }

    // 4. fill the caller's buffer with borrowed pointers to the group names.
    //    The strings live as long as the groups; the caller owns a_outNames
    //    and must not free the individual strings.
    ListItr itr = ListItrBegin(groups);
    ListItr end = ListItrEnd(groups);
    size_t count = 0;
    for (; count < a_capacity && itr != end; ++count)
    {
        a_outNames[count] = (const char*)ListItrGet(itr);
        itr = ListItrNext(itr);
    }

    *a_outCount = count;
    // groups remain that didn't fit -> caller's buffer was too small
    return (itr != end) ? USER_MANAGER_RESULT_INVALID_ARGUMENT
                        : USER_MANAGER_RESULT_SUCCESS;
}

UserManagerResult
UserManager_FormatAllUsersAndGroups(UserManager* a_manager, char* a_buf, size_t a_bufSize)
{
    if (a_manager == NULL || a_buf == NULL || a_bufSize == 0)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }

    a_buf[0] = '\0';
    FormatAllUsersContext ctx = {
        .m_buf = a_buf,
        .m_bufSize = a_bufSize,
        .m_offset = 0,
        .m_first = true,
    };

    HashMap_ForEach(a_manager->m_usersByName, AppendUserGroupsLine, &ctx);
    return USER_MANAGER_RESULT_SUCCESS;
}

static int
AppendUserGroupsLine(const void* a_key, void* a_value, void* a_context)
{
    (void)a_key;
    FormatAllUsersContext* ctx = (FormatAllUsersContext*)a_context;
    const User* user = (const User*)a_value;

    char line[CONF_USER_GROUPS_LINE_BUF_SIZE];
    if (FormatUserGroupsLine(user, line, sizeof(line)) != USER_MANAGER_RESULT_SUCCESS)
    {
        return 1; // skip this user, keep going
    }

    int written = snprintf(ctx->m_buf + ctx->m_offset,
                           ctx->m_bufSize - ctx->m_offset,
                           ctx->m_first ? "%s" : "\n%s",
                           line);
    if (written < 0)
    {
        LOG_ERROR("Failed to append user line to buffer, encoding error");
        return 0;
    }
    if ((size_t)written >= ctx->m_bufSize - ctx->m_offset)
    {
        LOG_WARN("User list truncated to fit buffer");
        return 0; // buffer full, stop iterating
    }

    ctx->m_offset += (size_t)written;
    ctx->m_first = false;
    return 1;
}

static size_t
UserManagerHashFunctionFd(const void* a_key)
{
    return (size_t)*(int*)a_key;
}

static int 
UserManagerEqualFunctionFd(const void* a_firstKey, const void* a_secondKey)
{
    return *(int*)a_firstKey == *(int*)a_secondKey;
}