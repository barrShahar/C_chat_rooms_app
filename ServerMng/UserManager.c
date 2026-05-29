#include <stdlib.h>
#include <string.h>
#include "UserManager.h"
#include "Vector.h"
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

UserManagerResult UserManager_AddUserToGroup(UserManager* a_manager, const int a_fdConnection, const char* a_groupName)
{
    if (a_manager == NULL || a_groupName == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }
    User* user;
    HashMap_Find(a_manager->m_usersByFd, &a_fdConnection, (void**)&user);
    if (user == NULL)
    {
        LOG_ERROR("User not found");
        return USER_MANAGER_RESULT_NOT_FOUND;
    }
    if (user->m_state == USER_STATE_OFFLINE)
    {
        LOG_ERROR("User is offline");
        return USER_MANAGER_RESULT_NOT_LOGGED;
    }
    if (user->m_groups == NULL)
    {
        user->m_groups = VectorCreate(CONF_USER_VECTOR_INITIAL_CAPACITY, CONF_USER_VECTOR_BLOCK_SIZE);
    }
    if (user->m_groups == NULL)
    {
        LOG_ERROR("Failed to create vector for user groups");
        return USER_MANAGER_RESULT_ALLOCATION_FAILED;
    }
    char* groupName = strdup(a_groupName);
    if (groupName == NULL)
    {
        LOG_ERROR("Failed to copy group name");
        return USER_MANAGER_RESULT_ALLOCATION_FAILED;
    }
    VectorAppend(user->m_groups, groupName);
    LOG_INFO("User %s added to group %s", user->m_username, a_groupName);
    return USER_MANAGER_RESULT_SUCCESS;
}
UserManagerResult UserManager_RemoveUserFromGroup(UserManager* a_manager, const int a_fdConnection, const char* a_groupName)
{
    if (a_manager == NULL || a_groupName == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }
    (void)a_fdConnection;
    return 1;
}

void UserManager_Destroy(UserManager** a_manager)
{
    if (a_manager == NULL || *a_manager == NULL)
    {
        return;
    }

    free(*a_manager);
    *a_manager = NULL;
}


UserManagerResult 
UserManager_AddUser(UserManager* a_manager, const int a_fdConnection, const char* a_username, const char* a_password)
{
    if (a_manager == NULL || a_username == NULL || a_password == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }
    LOG_DEBUG("Adding user: %s, password: %s", a_username, a_password);
    User* user = User_Create(a_fdConnection, a_username, a_password);
    if (user == NULL)
    {
        return USER_MANAGER_RESULT_ALLOCATION_FAILED;
    }
    MapResult insertResult = HashMap_Insert(a_manager->m_usersByName, user->m_username, user);
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

    insertResult = HashMap_Insert(a_manager->m_usersByFd, &user->m_fdConnection, user);
    if (insertResult != MAP_SUCCESS)
    {
        LOG_ERROR("Failed to insert user into hash map by fd");
        HashMap_Remove(a_manager->m_usersByName, a_username, NULL, NULL);
        User_Destroy(&user);
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }

    user->m_state = USER_STATE_ONLINE;
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
    HashMap_Remove(a_manager->m_usersByName, user->m_username, NULL, NULL);
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
    if (user->m_state == USER_STATE_ONLINE)
    {
        return USER_MANAGER_RESULT_ALREADY_LOG;
    }
    if (strcmp(user->m_password, a_password) != 0)
    {
        return USER_MANAGER_RESULT_BAD_CREDS;
    }
    user->m_fdConnection = a_fdConnection;
    MapResult insertResult = HashMap_Insert(a_manager->m_usersByFd, &user->m_fdConnection, user);
    if (insertResult != MAP_SUCCESS)
    {
        LOG_ERROR("Failed to insert user into hash map by fd");
        return USER_MANAGER_RESULT_INTERNAL_ERROR;
    }

    user->m_state = USER_STATE_ONLINE;
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
    if (user->m_state == USER_STATE_OFFLINE)
    {
        return USER_MANAGER_RESULT_NOT_LOGGED;
    }

    user->m_state = USER_STATE_OFFLINE;
    HashMap_Remove(a_manager->m_usersByFd, &a_fdConnection, NULL, NULL);
    if (a_loggedOutUsername != NULL)
    {
        *a_loggedOutUsername = networkCopyString(user->m_username);
    }
    user->m_fdConnection = -1; // to indicate that the user is not logged in
    return USER_MANAGER_RESULT_SUCCESS;
}

static size_t UserManagerHashFunctionFd(const void* a_key)
{
    return (size_t)*(int*)a_key;
}

static int UserManagerEqualFunctionFd(const void* a_firstKey, const void* a_secondKey)
{
    return *(int*)a_firstKey == *(int*)a_secondKey;
}