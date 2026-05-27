#ifndef __GROUP_MANAGER_H__
#define __GROUP_MANAGER_H__

typedef struct GroupManager GroupManager;

/**
 * @brief Create a group manager
 *
 * @return A pointer to the created group manager
 * @retval NULL on allocation failure
 */
GroupManager* GroupManager_Create(void);

/**
 * @brief Destroy a group manager
 * @details Releases all owned resources. On completion *a_manager will be NULL.
 *
 * @param[in,out] a_manager Address of a previously created GroupManager pointer
 */
void GroupManager_Destroy(GroupManager** a_manager);

#endif /* __GROUP_MANAGER_H__ */
