#include "User.h"
#include <stdlib.h>
#include <string.h>
#include "network_utils.h"
#include "logger.h"

typedef struct User
{
    int m_fdConnection;
    char* m_username;
    char* m_password;
    UserState m_state;
    List* m_groups;
} User;

User* User_Create(const int a_fdConnection, const char* a_username, const char* a_password)
{
    User* user = (User*)malloc(sizeof(User));
    if (user == NULL)
    {
        return NULL;
    }
    user->m_fdConnection = a_fdConnection;
    user->m_username = networkCopyString(a_username);
    user->m_password = networkCopyString(a_password);
    if (user->m_username == NULL || user->m_password == NULL)
    {
        if (user->m_username != NULL) free(user->m_username);
        if (user->m_password != NULL) free(user->m_password);
        LOG_ERROR("Failed to copy username or password");
        free(user);
        return NULL;
    }
    LOG_INFO("User created: %s, password: %s", user->m_username, user->m_password);
    user->m_state = USER_STATE_OFFLINE;
    user->m_groups = NULL;
    return user;
}

void 
User_Destroy(User** a_user)
{
    if (a_user == NULL || *a_user == NULL)
    {
        return;
    }

    if ((*a_user)->m_groups != NULL)
    {
        ListDestroy(&(*a_user)->m_groups, free);
    }
    free((*a_user)->m_username);
    free((*a_user)->m_password);
    free(*a_user);
    *a_user = NULL;
    
    LOG_INFO("User destroyed");
}

UserResult 
User_DestroyGroups(User* a_user)
{
    if (a_user == NULL)
    {
        return USER_RESULT_NULL_PTR;
    }
    if (a_user->m_groups == NULL)
    {
        return USER_RESULT_SUCCESS;
    }

    ListDestroy(&a_user->m_groups, free);
    a_user->m_groups = NULL;
    return USER_RESULT_SUCCESS;
}

UserResult 
User_SetState(User* a_user, UserState a_state)
{
    if (a_user == NULL)
    {
        return USER_RESULT_NULL_PTR;
    }
    if (a_state == a_user->m_state)
    {
        LOG_WARN("User state is already set to %d", a_state);
        return USER_RESULT_SUCCESS;
    }
    a_user->m_state = a_state;
    return USER_RESULT_SUCCESS;
}

UserState 
User_GetState(User* a_user)
{
    if (a_user == NULL)
    {
        LOG_ERROR("User is NULL");
        return USER_STATE_OFFLINE;
    }
    return a_user->m_state;
}

ListItr 
User_IsInGroup(const User* a_user, const char* a_groupName)
{
    if (a_user == NULL || a_groupName == NULL || a_user->m_groups == NULL)
    {
        return NULL;
    }

    return ListFind(a_user->m_groups, a_groupName, (int (*)(const void*, const void*))strcmp);
}

UserResult 
User_AddGroup(User* a_user, const char* a_groupName)
{
    // 1. parameters check
    if (a_user == NULL || a_groupName == NULL)
    {
        return USER_RESULT_NULL_PTR;
    }

    if (a_user->m_groups == NULL)
    {
        a_user->m_groups = ListCreate();
        if (a_user->m_groups == NULL)
        {
            LOG_ERROR("Failed to create list for user groups");
            return USER_RESULT_ALLOCATION_FAILED;
        }
        char* groupName = strdup(a_groupName);
        if (groupName == NULL)
        {
            LOG_ERROR("Failed to copy group name");
            return USER_RESULT_ALLOCATION_FAILED;
        }
        ListPushTail(a_user->m_groups, (void*)groupName);
        return USER_RESULT_SUCCESS;
    } 

    // 2. check if user is already in the group
    ListItr itr = User_IsInGroup(a_user, a_groupName);
    if (itr != NULL)
    {
        return USER_RESULT_ALREADY_IN_GROUP;
    }

    // 3. add group to user's groups
    char* groupName = strdup(a_groupName);
    if (groupName == NULL)
    {
        LOG_ERROR("Failed to copy group name");
        return USER_RESULT_ALLOCATION_FAILED;
    }
    ListPushTail(a_user->m_groups, (void*)groupName);
    return USER_RESULT_SUCCESS;
}

UserResult 
User_RemoveGroup(User* a_user, const char* a_groupName)
{
    // 1. parameters check
    if (a_user == NULL || a_groupName == NULL)
    {
        LOG_ERROR("User or group name is NULL");
        return USER_RESULT_NULL_PTR;
    }

    // 2. check if user is in the group
    ListItr itr = User_IsInGroup(a_user, a_groupName);
    if (itr == NULL)
    {
        LOG_DEBUG("User is not in the group");
        return USER_RESULT_NOT_IN_GROUP;
    }

    // 3. remove group from user's groups
    void* groupName = ListItrRemove(itr); 
    SOFT_ASSERT(groupName != NULL);
    if (groupName == NULL)
    {
        LOG_ERROR("Failed to remove group");
        return USER_RESULT_INTERNAL_ERROR;
    }
    free(groupName);
    return USER_RESULT_SUCCESS;
}

const int* 
User_GetFdConnection(const User* a_user)
{
    if (a_user == NULL)
    {
        LOG_ERROR("User is NULL");
        return NULL;
    }
    return &a_user->m_fdConnection;
}


const char* User_GetUsername(const User* a_user)
{
    if (a_user == NULL)
    {
        LOG_ERROR("User is NULL");
        return NULL;
    }
    return a_user->m_username;
}

const char* User_GetPassword(const User* a_user)
{
    if (a_user == NULL)
    {
        LOG_ERROR("User is NULL");
        return NULL;
    }
    return a_user->m_password;
}

UserResult User_GetGroups(User* a_user, List** a_groups)
{
    if (a_user == NULL || a_groups == NULL)
    {
        return USER_RESULT_NULL_PTR;
    }
    *a_groups = a_user->m_groups;
    return USER_RESULT_SUCCESS;
}

UserResult User_SetFdConnection(User* a_user, int a_fdConnection)
{
    if (a_user == NULL)
    {
        return USER_RESULT_NULL_PTR;
    }
    a_user->m_fdConnection = a_fdConnection;
    return USER_RESULT_SUCCESS;
}