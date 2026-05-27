#include "UserManager.h"
#include "logger.h"
#include <stdlib.h>
#include "HashMap.h"
#include "TcpConnectionRecord.h"

struct UserManager
{
    HashMap* m_users;
    size_t (*m_hashFunction)(const void* _key);
    int (*m_equalFunction)(const void* _firstKey, const void* _secondKey);
};

typedef struct User
{

} User;

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