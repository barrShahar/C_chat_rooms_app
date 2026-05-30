#pragma once
#include "db/gen_dlist.h"
#include "enum_helper.h"

typedef enum User_Result
{
    USER_RESULT_SUCCESS = 0,
    USER_RESULT_NULL_PTR,
    USER_RESULT_INVALID_ARGUMENT,
    USER_RESULT_ALLOCATION_FAILED,
    USER_RESULT_INTERNAL_ERROR,
    USER_RESULT_BAD_CREDS,
    USER_RESULT_NAME_TAKEN,
    USER_RESULT_NOT_FOUND,
    USER_RESULT_NOT_IN_GROUP,
    USER_RESULT_ALREADY_IN_GROUP,
    USER_RESULT_NOT_LOGGED,
    USER_RESULT_ALREADY_LOG,
    USER_RESULT_MALFORMED,
} UserResult;

typedef enum UserState
{
    USER_STATE_OFFLINE = 0,
    USER_STATE_ONLINE = 1,
} UserState;

typedef struct User User;


User* User_Create(const int a_fdConnection, const char* a_username, const char* a_password);
void User_Destroy(User** a_user);


// Groups related functions
ListItr User_IsInGroup(const User* a_user, const char* a_groupName);
UserResult User_AddGroup(User* a_user, const char* a_groupName);
UserResult User_RemoveGroup(User* a_user, const char* a_groupName);
UserResult User_GetGroups(User* a_user, List** a_groups);
UserResult User_DestroyGroups(User* a_user);

// Getters and setters
UserState User_GetState(User* a_user);
UserResult User_SetState(User* a_user, UserState a_state);
const int* User_GetFdConnection(const User* a_user);
UserResult User_SetFdConnection(User* a_user, int a_fdConnection);
const char* User_GetUsername(const User* a_user);
const char* User_GetPassword(const User* a_user);
