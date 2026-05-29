#include "Group.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include "network_utils.h"

struct Group
{
    char* m_name;
    GroupEndpoint m_endpoint;
    size_t m_refCount;
    int m_udpFd;
};

Group*
Group_Create(const char* a_name, const GroupEndpoint* a_endpoint)
{
    if (a_name == NULL)
    {
        LOG_ERROR("Group name is NULL");
        return NULL;
    }

    Group* group = (Group*)malloc(sizeof(Group));
    if (group == NULL)
    {
        LOG_ERROR("Failed to allocate memory for group");
        return NULL;
    }

    group->m_name = networkCopyString(a_name);
    if (group->m_name == NULL)
    {
        LOG_ERROR("Failed to copy group name");
        free(group);
        return NULL;
    }

    if (a_endpoint != NULL)
    {
        group->m_endpoint = *a_endpoint;
    }
    else
    {
        memset(&group->m_endpoint, 0, sizeof(group->m_endpoint));
    }

    group->m_refCount = 0;
    group->m_udpFd = -1;
    return group;
}

void
Group_Destroy(Group** a_group)
{
    if (a_group == NULL || *a_group == NULL)
    {
        LOG_ERROR("Tried to destroy group that is NULL");
        return;
    }

    free((*a_group)->m_name);
    free(*a_group);
    *a_group = NULL;
}

const char*
Group_GetName(const Group* a_group)
{
    if (a_group == NULL)
    {
        LOG_ERROR("Tried to get name of group that is NULL");
        return NULL;
    }
    return a_group->m_name;
}

GroupEndpoint
Group_GetEndpoint(const Group* a_group)
{
    GroupEndpoint empty = {0, 0};
    if (a_group == NULL)
    {
        LOG_ERROR("Tried to get endpoint of group that is NULL");
        return empty;
    }
    return a_group->m_endpoint;
}

size_t
Group_GetRefCount(const Group* a_group)
{
    if (a_group == NULL)
    {
        LOG_ERROR("Tried to get ref count of group that is NULL");
        return 0;
    }
    return a_group->m_refCount;
}

size_t
Group_IncRef(Group* a_group)
{
    if (a_group == NULL)
    {
        LOG_ERROR("Tried to increment ref count of group that is NULL");
        return 0;
    }
    a_group->m_refCount++;
    return a_group->m_refCount;
}

size_t
Group_DecRef(Group* a_group)
{
    if (a_group == NULL)
    {
        LOG_ERROR("Tried to decrement ref count of group that is NULL");
        return 0;
    }
    if (a_group->m_refCount == 0)
    {
        LOG_ERROR("Group ref count is already 0");
        return 0;
    }
    a_group->m_refCount--;
    return a_group->m_refCount;
}
