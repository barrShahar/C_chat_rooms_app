#pragma once

typedef enum UserState
{
    USER_STATE_OFFLINE = 0,
    USER_STATE_ONLINE = 1,
} UserState;

typedef struct User
{
    int m_fdConnection;
    char* m_username;
    char* m_password;
    UserState m_state;
} User;

User* User_Create(const int a_fdConnection, const char* a_username, const char* a_password);
void User_Destroy(User** a_user);
void User_SetState(User* a_user, UserState a_state);
UserState User_GetState(User* a_user);
int User_GetFdConnection(User* a_user);
char* User_GetUsername(User* a_user);
char* User_GetPassword(User* a_user);