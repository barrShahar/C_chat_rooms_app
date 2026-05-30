#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "GroupManager.h"
#include "logger.h"
#include "HashMap.h"
#include "config.h"
#include "Group.h"
#include "network_utils.h"

struct GroupManager
{
    HashMap* m_groups;
};


static void DestroyGroupHashMapValue(void* a_value);   // For HashMapDestroy
static GroupManagerResult AllocateGroupEndpoint(const HashMap* a_groups, GroupEndpoint* a_outEndpoint);


typedef struct FormatGroupListContext
{
    char* m_buf;
    size_t m_bufSize;
    size_t m_offset;
    bool m_first;
} FormatGroupListContext;


static int
appendGroupName(const void* a_key, void* a_value, void* a_context)
{
    (void)a_value;
    FormatGroupListContext* ctx = (FormatGroupListContext*)a_context;
    const char* name = (const char*)a_key;
    int written = 0;

    if (!ctx->m_first)
    {
        written = snprintf(ctx->m_buf + ctx->m_offset,
                           ctx->m_bufSize - ctx->m_offset,
                           ", %s",
                           name);
    }
    else
    {
        written = snprintf(ctx->m_buf + ctx->m_offset,
                           ctx->m_bufSize - ctx->m_offset,
                           "%s",
                           name);
        ctx->m_first = false;
    }

    if (written < 0)
    {
        LOG_ERROR("Failed to append group name to buffer, Encoding error");
        return 0;
    }
    if ((size_t)written >= ctx->m_bufSize - ctx->m_offset)
    {
        LOG_WARN("Failed to append group name to buffer, Buffer overflow");
        return 0;
    }

    ctx->m_offset += (size_t)written;
    return 1;
}

GroupManager*
GroupManager_Create(size_t (*a_hashFunction)(const void* _key),
                    int (*a_equalFunction)(const void* _firstKey, const void* _secondKey))
{
    GroupManager* manager = (GroupManager*)malloc(sizeof(GroupManager));
    if (manager == NULL)
    {
        return NULL;
    }
    manager->m_groups = HashMap_Create(CONF_GROUP_MANAGER_HASH_MAP_SIZE,
                                       a_hashFunction,
                                       a_equalFunction);
    if (manager->m_groups == NULL)
    {
        LOG_ERROR("Failed to create hash map for groups");
        free(manager);
        return NULL;
    }
    return manager;
}

void
GroupManager_Destroy(GroupManager** a_manager)
{
    if (a_manager == NULL || *a_manager == NULL)
    {
        return;
    }

    HashMap_Destroy(&(*a_manager)->m_groups, NULL, DestroyGroupHashMapValue);
    free(*a_manager);
    *a_manager = NULL;
}

GroupManagerResult
GroupManager_AddGroup(GroupManager* a_manager, const char* a_name)
{
    if (a_manager == NULL || a_name == NULL)
    {
        LOG_ERROR("Group manager or name is NULL");
        return GROUP_MANAGER_RESULT_NULL_PTR;
    }
    if (strlen(a_name) > CONF_GROUP_MANAGER_MAX_NAME_LENGTH)
    {
        LOG_ERROR("Group name is too long");
        return GROUP_MANAGER_RESULT_NAME_TOO_LONG;
    }

    // Check if group name is already taken
    if (GroupManager_IsNameTaken(a_manager, a_name))
    {
        LOG_ERROR("Group name is already taken");
        return GROUP_MANAGER_RESULT_NAME_TAKEN;
    }

    GroupEndpoint endpoint; // For UDP connection
    GroupManagerResult allocResult = AllocateGroupEndpoint(a_manager->m_groups, &endpoint);
    if (allocResult != GROUP_MANAGER_RESULT_SUCCESS)
    {
        return allocResult;
    }

    Group* group = Group_Create(a_name, &endpoint);
    if (group == NULL)
    {
        LOG_ERROR("Failed to create group");

        return GROUP_MANAGER_RESULT_ALLOCATION_FAILED;
    }

    MapResult insertResult = HashMap_Insert(a_manager->m_groups, Group_GetName(group), group);
    if (insertResult != MAP_SUCCESS)
    {
        LOG_ERROR("Failed to insert group into hash map");
        Group_Destroy(&group);
        if (insertResult == MAP_ALLOCATION_ERROR)
        {
            return GROUP_MANAGER_RESULT_ALLOCATION_FAILED;
        }
        return GROUP_MANAGER_RESULT_INTERNAL_ERROR;
    }

    return GROUP_MANAGER_RESULT_SUCCESS;
}

bool
GroupManager_IsNameTaken(GroupManager* a_groupManager, const char* a_name)
{
    if (a_groupManager == NULL || a_name == NULL)
    {
        LOG_ERROR("Group manager or name is NULL");
        return false;
    }
    return HashMap_Find(a_groupManager->m_groups, a_name, NULL) == MAP_SUCCESS;
}

GroupManagerResult
GroupManager_GetGroup(GroupManager* a_manager, const char* a_name, Group** a_outGroup)
{
    if (a_manager == NULL || a_name == NULL || a_outGroup == NULL)
    {
        return GROUP_MANAGER_RESULT_NULL_PTR;
    }

    void* value = NULL;
    if (HashMap_Find(a_manager->m_groups, a_name, &value) != MAP_SUCCESS)
    {
        return GROUP_MANAGER_RESULT_NOT_FOUND;
    }

    *a_outGroup = (Group*)value;
    return GROUP_MANAGER_RESULT_SUCCESS;
}

GroupManagerResult
GroupManager_GetGroupEndpoint(GroupManager* a_manager, const char* a_name, GroupEndpoint* a_outEndpoint)
{
    if (a_manager == NULL || a_name == NULL || a_outEndpoint == NULL)
    {
        return GROUP_MANAGER_RESULT_NULL_PTR;
    }

    Group* group = NULL;
    GroupManagerResult result = GroupManager_GetGroup(a_manager, a_name, &group);
    if (result != GROUP_MANAGER_RESULT_SUCCESS)
    {
        return result;
    }

    *a_outEndpoint = Group_GetEndpoint(group);
    return GROUP_MANAGER_RESULT_SUCCESS;
}

GroupManagerResult
GroupManager_RemoveGroupIfEmpty(GroupManager* a_manager, const char* a_name)
{
    Group* group = NULL;
    GroupManagerResult lookup = GroupManager_GetGroup(a_manager, a_name, &group);
    if (lookup != GROUP_MANAGER_RESULT_SUCCESS)
    {
        return lookup;
    }

    if (Group_GetRefCount(group) > 0)
    {
        return GROUP_MANAGER_RESULT_NOT_EMPTY;
    }

    void* removedKey = NULL;
    void* removedValue = NULL;
    MapResult removeResult = HashMap_Remove(a_manager->m_groups, a_name, &removedKey, &removedValue);
    if (removeResult != MAP_SUCCESS)
    {
        return GROUP_MANAGER_RESULT_INTERNAL_ERROR;
    }

    Group_Destroy((Group**)&removedValue);
    return GROUP_MANAGER_RESULT_SUCCESS;
}

/** 
 * @brief Format a list of group names into a buffer
 * @param[in] a_manager - Group manager
 * @param[in] a_buf - Buffer to format the group names into
 * @param[in] a_bufSize - Size of the buffer
 * @return GROUP_MANAGER_RESULT_SUCCESS on success, GROUP_MANAGER_RESULT_NULL_PTR on error
 */
GroupManagerResult
GroupManager_FormatGroupList(GroupManager* a_manager, char* a_buf, size_t a_bufSize)
{
    if (a_manager == NULL || a_buf == NULL || a_bufSize == 0)
    {
        LOG_ERROR("GROUP_MANAGER_RESULT_NULL_PTR");
        return GROUP_MANAGER_RESULT_NULL_PTR;
    }
    
    a_buf[0] = '\0';
    FormatGroupListContext ctx = {
        .m_buf = a_buf,
        .m_bufSize = a_bufSize,
        .m_offset = 0,
        .m_first = true,
    };
    
    HashMap_ForEach(a_manager->m_groups, appendGroupName, &ctx);
    return GROUP_MANAGER_RESULT_SUCCESS;
}

static void
DestroyGroupHashMapValue(void* a_value)
{
    Group* group = (Group*)a_value;
    Group_Destroy(&group);
}

static GroupManagerResult
AllocateGroupEndpoint(const HashMap* a_groups, GroupEndpoint* a_outEndpoint)
{
    size_t index = HashMap_Size(a_groups);
    if (index >= CONF_MULTICAST_MAX_GROUPS)
    {
        LOG_ERROR("No multicast ports left for new groups");
        return GROUP_MANAGER_RESULT_INTERNAL_ERROR;
    }

    a_outEndpoint->m_multicastAddr = network_convert_ip_p_to_n(CONF_MULTICAST_BASE_IP);
    a_outEndpoint->m_port = (uint16_t)(CONF_MULTICAST_PORT_BASE + index);
    return GROUP_MANAGER_RESULT_SUCCESS;
}

size_t
GroupManager_GetGroupsCount(const GroupManager* a_manager)
{
    if (a_manager == NULL)
    {
        LOG_WARN("Group manager is NULL");
        return 0;
    }
    
    return HashMap_Size(a_manager->m_groups);
}

GroupManagerResult 
GroupManager_IncreaseGroupRefCount(GroupManager* a_manager, const char* a_name, size_t* a_outRefCount)
{
    
    if (a_manager == NULL || a_name == NULL)
    {
        return GROUP_MANAGER_RESULT_NULL_PTR;
    }

    Group* group = NULL;
    GroupManager_GetGroup(a_manager, a_name, &group);
    if (group == NULL)
    {
        LOG_ERROR("Group not found");
        return GROUP_MANAGER_RESULT_NOT_FOUND;
    }
    size_t newRefCount = Group_IncRef(group);
    if (a_outRefCount != NULL)
    {
        *a_outRefCount = newRefCount;
    }
    return GROUP_MANAGER_RESULT_SUCCESS;
}

GroupManagerResult 
GroupManager_DecreaseGroupRefCount(GroupManager* a_manager, const char* a_name, size_t* a_outRefCount)
{
    // 1. parameters check
    if (a_manager == NULL || a_name == NULL)
    {
        return GROUP_MANAGER_RESULT_NULL_PTR;
    }

    if (a_outRefCount != NULL)
    {
        *a_outRefCount = 0;
    }

    // 2. get group by name
    Group* group = NULL;
    GroupManager_GetGroup(a_manager, a_name, &group);
    if (group == NULL)
    {
        LOG_ERROR("Group not found");
        return GROUP_MANAGER_RESULT_NOT_FOUND;
    }

    // 3. Check if group is empty before decrementing ref count
    if (Group_GetRefCount(group) == 0)
    {
        return GROUP_MANAGER_RESULT_EMPTY_GROUP;
    }

    // 4. Decrease group ref count
    size_t newRefCount = Group_DecRef(group);
    if (newRefCount == 0)
    {
        LOG_INFO("Group %s ref count is 0", a_name);
    }
    if (a_outRefCount != NULL)
    {
        *a_outRefCount = newRefCount;
    }
    return GROUP_MANAGER_RESULT_SUCCESS;
}

