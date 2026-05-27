#include <stdlib.h>
#include "GroupManager.h"

struct GroupManager
{
    /* TODO: add group storage */
};

GroupManager*
GroupManager_Create(void)
{
    GroupManager* manager = (GroupManager*)malloc(sizeof(GroupManager));
    if (manager == NULL)
    {
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

    free(*a_manager);
    *a_manager = NULL;
}
