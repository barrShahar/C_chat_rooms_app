#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct Group Group;

typedef struct GroupEndpoint
{
    uint32_t m_multicastAddr;  /* network byte order, e.g. from network_convert_ip_p_to_n */
    uint16_t m_port;           /* host byte order or pick one convention and stick to it */
} GroupEndpoint;

Group* Group_Create(const char* a_name, const GroupEndpoint* a_endpoint);
void   Group_Destroy(Group** a_group);

const char*        Group_GetName(const Group* a_group);
GroupEndpoint      Group_GetEndpoint(const Group* a_group);

/* How many users are using this channel (for teardown policy) */
size_t Group_GetRefCount(const Group* a_group);
size_t Group_IncRef(Group* a_group);   /* returns new count; 0 on error */
size_t Group_DecRef(Group* a_group);   /* returns new count; no-op below 0 in debug assert */