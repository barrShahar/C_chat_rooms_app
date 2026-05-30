#ifndef __USER_MANAGER_H__
#define __USER_MANAGER_H__

#include <stddef.h>
#include <stdbool.h>
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
    _(USER_MANAGER_RESULT_NOT_IN_GROUP, ) \
    _(USER_MANAGER_RESULT_ALREADY_IN_GROUP, ) \
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

/**
 * @brief Get the number of groups a user is currently in.
 * @details Intended to be called before UserManager_GetUserGroups so the caller
 *          can size its buffer. An unknown fd yields a count of 0 (not an error).
 * @param[in]  a_manager     - the user manager
 * @param[in]  a_fdConnection - fd identifying the user
 * @param[out] a_outCount    - receives the group count (0 if the user is unknown)
 * @return The result of the operation
 * @retval USER_MANAGER_RESULT_SUCCESS on success
 * @retval USER_MANAGER_RESULT_NULL_PTR if a_manager or a_outCount is NULL
 * @retval USER_MANAGER_RESULT_INTERNAL_ERROR if the user's group list is unavailable
 */
 UserManagerResult UserManager_GetUserGroupCount(UserManager* a_manager, const int a_fdConnection,
                                                 size_t* a_outCount);

/**
 * @brief Fill a caller-provided array with the names of the groups a user is in.
 * @details Caller-allocates: the function writes nothing on the heap. It stores
 *          borrowed pointers to the group-name strings into a_outNames. Those
 *          strings are owned by the groups and are valid only while the groups
 *          live; the caller owns a_outNames itself but must NOT free the strings.
 * @param[in]  a_manager     - the user manager
 * @param[in]  a_fdConnection - fd identifying the user
 * @param[out] a_outNames    - caller-provided array of at least a_capacity entries
 * @param[in]  a_capacity    - number of entries a_outNames can hold
 * @param[out] a_outCount    - receives the number of names written
 * @return The result of the operation
 * @retval USER_MANAGER_RESULT_SUCCESS on success (all names fit)
 * @retval USER_MANAGER_RESULT_NULL_PTR if a_manager, a_outNames or a_outCount is NULL
 * @retval USER_MANAGER_RESULT_NOT_FOUND if no user maps to a_fdConnection
 * @retval USER_MANAGER_RESULT_INVALID_ARGUMENT if a_capacity was too small to hold
 *         every group (a_outCount entries were still written)
 */
 UserManagerResult UserManager_GetUserGroups(UserManager* a_manager, const int a_fdConnection,
                                             const char** a_outNames, size_t a_capacity,
                                             size_t* a_outCount);

/**
 * @brief Log every registered user alongside the groups they belong to.
 * @details Iterates all users and emits one log line per user in the form
 *          "<username> -> g1, g2, ..." (or "<username> -> (none)").
 *          Server-side display only; nothing is returned to the caller.
 * @param[in] a_manager - the user manager to dump
 * @return The result of the operation
 * @retval USER_MANAGER_RESULT_SUCCESS on success
 * @retval USER_MANAGER_RESULT_NULL_PTR if the user manager is NULL
 */
 UserManagerResult UserManager_GetAllUsersAndTheirGroups(UserManager* a_manager);

/**
 * @brief Format every user and their groups into a buffer (one line per user).
 * @details Writes lines of the form "<username> -> g1, g2, ..." separated by
 *          '\n'. Truncates cleanly to fit a_bufSize, so a wire-sized buffer is
 *          always safe. The buffer is null-terminated; the empty-table case
 *          yields an empty string.
 * @param[in]  a_manager - the user manager to dump
 * @param[out] a_buf     - destination buffer
 * @param[in]  a_bufSize - size of a_buf in bytes
 * @return The result of the operation
 * @retval USER_MANAGER_RESULT_SUCCESS on success
 * @retval USER_MANAGER_RESULT_NULL_PTR if a_manager or a_buf is NULL, or a_bufSize is 0
 */
 UserManagerResult UserManager_FormatAllUsersAndGroups(UserManager* a_manager, char* a_buf, size_t a_bufSize);

 UserManagerResult UserManager_IsUserLoggedIn(const UserManager* a_manager, 
                                              const char* a_username, 
                                              const int a_fdConnection, 
                                              bool* a_isLoggedIn);

UserManagerResult UserManager_IsUserInGroup(UserManager* a_manager, const int a_fdConnection, const char* a_groupName, bool* a_isInGroup);
#endif /* __USER_MANAGER_H__ */
