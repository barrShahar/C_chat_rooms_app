#include "UserManager.h"
#include "logger.h"
#include <stdlib.h>
#include "HashMap.h"
#include "User.h"


struct UserManager
{
    HashMap* m_users;
    size_t (*m_hashFunction)(const void* _key);
    int (*m_equalFunction)(const void* _firstKey, const void* _secondKey);
};


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
    userManager->m_users = HashMap_Create(10, a_hashFunction, a_equalFunction);
    if (userManager->m_users == NULL)
    {
        LOG_ERROR("Failed to create hash map for users");
        free(userManager);
        return NULL;
    }
    userManager->m_hashFunction = a_hashFunction;
    userManager->m_equalFunction = a_equalFunction;
    
    LOG_INFO("User manager created");
    return userManager;
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
    HashMap_Insert(a_manager->m_users, a_username, user);
    
    LOG_INFO("User added: %s", a_username);
    return USER_MANAGER_RESULT_SUCCESS;
}

UserManagerResult UserManager_RemoveUser(UserManager* a_manager, const char* a_username)
{
    if (a_manager == NULL || a_username == NULL)
    {
        return USER_MANAGER_RESULT_NULL_PTR;
    }
    return USER_MANAGER_RESULT_SUCCESS;
}

const char*
UserManagerResult_ToString(const UserManagerResult a_result)
{
    switch (a_result) {
        case USER_MANAGER_RESULT_SUCCESS:           return "SUCCESS";
        case USER_MANAGER_RESULT_NULL_PTR:          return "NULL_PTR";
        case USER_MANAGER_RESULT_INVALID_ARGUMENT:   return "INVALID_ARGUMENT";
        case USER_MANAGER_RESULT_ALLOCATION_FAILED: return "ALLOCATION_FAILED";
        case USER_MANAGER_RESULT_INTERNAL_ERROR:    return "INTERNAL_ERROR";
        case USER_MANAGER_RESULT_BAD_CREDS:         return "BAD_CREDS";
        case USER_MANAGER_RESULT_NAME_TAKEN:        return "NAME_TAKEN";
        case USER_MANAGER_RESULT_NOT_FOUND:         return "NOT_FOUND";
        case USER_MANAGER_RESULT_ALREADY_IN:        return "ALREADY_IN";
        case USER_MANAGER_RESULT_NOT_IN:            return "NOT_IN";
        case USER_MANAGER_RESULT_NOT_LOGGED:        return "NOT_LOGGED";
        case USER_MANAGER_RESULT_ALREADY_LOG:       return "ALREADY_LOG";
        case USER_MANAGER_RESULT_MALFORMED:         return "MALFORMED";
        default:                                    return "UNKNOWN_ERROR";
    }
}