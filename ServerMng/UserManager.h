#ifndef __USER_MANAGER_H__
#define __USER_MANAGER_H__

#include <stddef.h>

typedef struct UserManager UserManager;

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

#endif /* __USER_MANAGER_H__ */
