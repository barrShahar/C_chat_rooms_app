#ifndef __USER_MANAGER_H__
#define __USER_MANAGER_H__

#include "ServerManager.h"
#include <stddef.h>

typedef struct UserManager UserManager;

typedef enum UserManagerResult
{
    USER_MANAGER_RESULT_SUCCESS = 0,
    USER_MANAGER_RESULT_NULL_PTR,
    USER_MANAGER_RESULT_INVALID_ARGUMENT,
    USER_MANAGER_RESULT_ALLOCATION_FAILED,
    USER_MANAGER_RESULT_INTERNAL_ERROR,
    USER_MANAGER_RESULT_BAD_CREDS,
    USER_MANAGER_RESULT_NAME_TAKEN,
    USER_MANAGER_RESULT_NOT_FOUND,
    USER_MANAGER_RESULT_ALREADY_IN,
    USER_MANAGER_RESULT_NOT_IN,
    USER_MANAGER_RESULT_NOT_LOGGED,
    USER_MANAGER_RESULT_ALREADY_LOG,
    USER_MANAGER_RESULT_MALFORMED,
} UserManagerResult;

const char* UserManagerResult_ToString(const UserManagerResult a_result);
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
UserManagerResult UserManager_AddUser(UserManager* a_manager, const char* a_username, const char* a_password);

/**
 * @brief Remove a user from the user manager
 * @details Removes a user from the user manager with the given username
 * @param[in] a_manager - the user manager to remove the user from
 * @param[in] a_username - the username of the user to remove
 * @return The result of the operation
 * @retval SERVER_RESULT_SUCCESS on success
 * @retval SERVER_RESULT_NULL_PTR if the user manager is NULL
 * @retval SERVER_RESULT_INVALID_ARGUMENT if the username is NULL
 * @retval SERVER_RESULT_INTERNAL_ERROR if an internal error occurs
 */
 UserManagerResult UserManager_RemoveUser(UserManager* a_manager, const char* a_username);


#endif /* __USER_MANAGER_H__ */
