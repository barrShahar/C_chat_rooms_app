#ifndef __CLIENT_MENU_H__
#define __CLIENT_MENU_H__

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SESSION_DISCONNECTED,
    SESSION_CONNECTED,
    SESSION_LOGGED_IN,
} SessionState;

typedef enum {
    MENU_NONE = 0,
    MENU_CONNECT,
    MENU_REGISTER,
    MENU_LOGIN,
    MENU_LOGOUT,
    MENU_CREATE_GROUP,
    MENU_JOIN_GROUP,
    MENU_LEAVE_GROUP,
    MENU_DISPLAY_USERS,
    MENU_DISPLAY_GROUPS,
    MENU_EXIT,
} MenuChoice;

void       Menu_Render(SessionState a_state);
MenuChoice Menu_Parse(const char* a_line, SessionState a_state);
bool       Menu_ReadLine(const char* a_prompt, char* a_buf, size_t a_size);

#endif /* __CLIENT_MENU_H__ */
