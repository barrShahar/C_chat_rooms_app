#ifndef __USER_MANAGER_H__
#define __USER_MANAGER_H__

#include <stddef.h>
#include "ServerManager.h"
#include "enum_helper.h"



typedef struct UserManager UserManager;

#define USER_MANAGER_RESULT_TABLE(_) \
    _(USER_MANAGER_RESULT_SUCCESS, = 0) \
    _(USER_MANAGER_RESULT_NULL_PTR, ) \
    _(USER_MANAGER_RESULT_INVALID_ARGUMENT, ) \
    _(USER_MANAGER_RESULT_ALLOCATION_FAILED, ) \
    _(USER_MANAGER_RESULT_INTERNAL_ERROR, ) \
    _(USER_MANAGER_RESULT_BAD_CREDS, ) \
    _(USER_MANAGER_RESULT_NAME_TAKEN, ) \
    _(USER_MANAGER_RESULT_NOT_FOUND, ) \
    _(USER_MANAGER_RESULT_ALREADY_IN, ) \
    _(USER_MANAGER_RESULT_NOT_IN, ) \
    _(USER_MANAGER_RESULT_NOT_LOGGED, ) \
    _(USER_MANAGER_RESULT_ALREADY_LOG, ) \
    _(USER_MANAGER_RESULT_MALFORMED, )

DEFINE_ENUM(UserManagerResult, USER_MANAGER_RESULT_TABLE)
DEFINE_ENUM_TO_STRING(UserManagerResult, USER_MANAGER_RESULT_TABLE)

/**
 * @brief Create a user manager
 * 
 * @param[in] a_hashFunction - hash function for users
 * @param[in] a_equalFunction - equality function for users
 * @return A pointer to the created user manager
 * @retval NULL on allocation failure
 */
 UserManager* UserManager_Create(size_t (
    *a_hashFunction)(const void* _key),
    int (*a_equalFunction)(const void* _firstKey, const void* _secondKey));

/**
 * @brief Destroy a user manager
 * @details Releases all owned resources. On completion *a_manager will be NULL.
 *
 * @param[in,out] a_manager Address of a previously created UserManager pointer
 */
void UserManager_Destroy(UserManager** a_manager);

/**
 * @brief Add a user to the user manager
 * @details Adds a user to the user manager with the given username and password
 * @param[in] a_manager - the user manager to add the user to
 * @param[in] a_username - the username of the user to add
 * @param[in] a_password - the password of the user to add
 * @return The result of the operation
 * @retval SERVER_RESULT_SUCCESS on success
 * @retval SERVER_RESULT_NULL_PTR if the user manager is NULL
 * @retval SERVER_RESULT_INVALID_ARGUMENT if the username or password is NULL
 * @retval SERVER_RESULT_ALLOCATION_FAILED if the user cannot be added
 * @retval SERVER_RESULT_INTERNAL_ERROR if an internal error occurs
 */
UserManagerResult UserManager_AddUser(UserManager* a_manager, const int a_fdConnection, const char* a_username, const char* a_password);

/**
 * @brief Remove a user from the user manager
 * @details Removes a user from the user manager with the given username
 * @param[in] a_manager - the user manager to remove the user from
 * @param[in] a_fdConnection - the fd connection of the user to remove
 * @return The result of the operation
 * @retval SERVER_RESULT_SUCCESS on success
 * @retval SERVER_RESULT_NULL_PTR if the user manager is NULL
 * @retval SERVER_RESULT_INVALID_ARGUMENT if the username is NULL
 * @retval SERVER_RESULT_INTERNAL_ERROR if an internal error occurs
 */
 UserManagerResult UserManager_RemoveUser(UserManager* a_manager, const int a_fdConnection);

 UserManagerResult UserManager_Login(UserManager* a_manager, const int a_fdConnection, const char* a_username, const char* a_password);

 UserManagerResult UserManager_Logout(UserManager* a_manager, const int a_fdConnection, char** a_loggedOutUsername);

 UserManagerResult UserManager_AddUserToGroup(UserManager* a_manager, const int a_fdConnection, const char* a_groupName);

 UserManagerResult UserManager_RemoveUserFromGroup(UserManager* a_manager, const int a_fdConnection, const char* a_groupName);

 UserManagerResult UserManager_GetUsersInGroup(UserManager* a_manager, const char* a_groupName, char*** a_users);

 #endif /* __USER_MANAGER_H__ */
