#ifndef __GROUP_MANAGER_H__
#define __GROUP_MANAGER_H__

#include <stddef.h>
#include <stdbool.h>
#include "Group.h"
#include "enum_helper.h"



typedef struct GroupManager GroupManager;

/* ========================================================================== */
/* 1. THE MASTER TABLE       */
/* ========================================================================== */
#define GROUP_MANAGER_RESULT_TABLE(_) \
    _(GROUP_MANAGER_RESULT_SUCCESS, = 0) \
    _(GROUP_MANAGER_RESULT_NULL_PTR, ) \
    _(GROUP_MANAGER_RESULT_NAME_TAKEN, ) \
    _(GROUP_MANAGER_RESULT_NAME_TOO_LONG, ) \
    _(GROUP_MANAGER_RESULT_ALLOCATION_FAILED, ) \
    _(GROUP_MANAGER_RESULT_NOT_FOUND, ) \
    _(GROUP_MANAGER_RESULT_EMPTY_GROUP, ) \
    _(GROUP_MANAGER_RESULT_NOT_EMPTY, ) \
    _(GROUP_MANAGER_RESULT_INTERNAL_ERROR, )

/* ========================================================================== */
/* 2. AUTOMATIC GENERATION                */
/* ========================================================================== */
DEFINE_ENUM(GroupManagerResult, GROUP_MANAGER_RESULT_TABLE)
DEFINE_ENUM_TO_STRING(GroupManagerResult, GROUP_MANAGER_RESULT_TABLE)


/**
 * @brief Create a group manager
 *
 * @param[in] a_hashFunction - hash function for group names (strings)
 * @param[in] a_equalFunction - equality function for group names
 * @return A pointer to the created group manager, or NULL on allocation failure
 */
GroupManager* GroupManager_Create(size_t (*a_hashFunction)(const void* _key),
                                  int (*a_equalFunction)(const void* _firstKey,
                                                         const void* _secondKey));

/**
 * @brief Destroy a group manager
 * @details Releases all owned groups. On completion *a_manager will be NULL.
 */
void GroupManager_Destroy(GroupManager** a_manager);

GroupManagerResult GroupManager_AddGroup(GroupManager* a_manager, 
                                         const char* a_name);

bool GroupManager_IsNameTaken(GroupManager* a_groupManager, 
                              const char* a_name);

/**
 * @brief Look up a group by name (non-owning pointer).
 */
GroupManagerResult GroupManager_GetGroup(GroupManager* a_manager,
                                         const char* a_name,
                                         Group** a_outGroup);

/**
 * @brief Remove a group only when no users reference it (ref count is 0).
 */
GroupManagerResult GroupManager_RemoveGroupIfEmpty(GroupManager* a_manager,
                                                 const char* a_name);

/**
 * @brief Write a comma-separated list of group names into a_buf.
 */
GroupManagerResult GroupManager_FormatGroupList(GroupManager* a_manager,
                                                char* a_buf,
                                                size_t a_bufSize);

size_t GroupManager_GetGroupsCount(const GroupManager* a_manager);

GroupManagerResult GroupManager_IncreaseGroupRefCount(GroupManager* a_manager, const char* a_name, size_t* a_outRefCount);
GroupManagerResult GroupManager_DecreaseGroupRefCount(GroupManager* a_manager, const char* a_name, size_t* a_outRefCount);


#endif /* __GROUP_MANAGER_H__ */
