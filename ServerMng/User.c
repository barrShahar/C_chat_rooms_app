#include "User.h"
#include <stdlib.h>
#include <string.h>
#include "network_utils.h"
#include "logger.h"

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
    return user;
}

void User_Destroy(User** a_user)
{
    if (a_user == NULL || *a_user == NULL)
    {
        return;
    }

    free((*a_user)->m_username);
    free((*a_user)->m_password);
    free(*a_user);
    *a_user = NULL;
    LOG_INFO("User destroyed");
}

void User_SetState(User* a_user, UserState a_state)
{
    if (a_user == NULL)
    {
        return;
    }
    a_user->m_state = a_state;
}

UserState User_GetState(User* a_user)
{
    if (a_user == NULL)
    {
        LOG_ERROR("User is NULL");
        return USER_STATE_OFFLINE;
    }
    return a_user->m_state;
}