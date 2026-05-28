#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "Menu.h"

static const MenuChoice s_disconnectedOptions[] = {
    MENU_CONNECT,
    MENU_EXIT,
};

static const MenuChoice s_connectedOptions[] = {
    MENU_REGISTER,
    MENU_LOGIN,
    MENU_EXIT,
};

static const MenuChoice s_loggedInOptions[] = {
    MENU_LOGOUT,
    MENU_CREATE_GROUP,
    MENU_JOIN_GROUP,
    MENU_LEAVE_GROUP,
    MENU_DISPLAY_USERS,
    MENU_DISPLAY_GROUPS,
    MENU_EXIT,
};

static const char* ChoiceLabel(MenuChoice a_choice)
{
    switch (a_choice)
    {
        case MENU_CONNECT:      return "Connect";
        case MENU_REGISTER:     return "Register";
        case MENU_LOGIN:        return "Login";
        case MENU_LOGOUT:       return "Logout";
        case MENU_CREATE_GROUP:  return "Create group";
        case MENU_JOIN_GROUP:    return "Join group";
        case MENU_LEAVE_GROUP:   return "Leave group";
        case MENU_DISPLAY_USERS: return "Display users";
        case MENU_DISPLAY_GROUPS:return "Display groups";
        case MENU_EXIT:          return "Exit";
        default:                return "?";
    }
}

static void GetOptions(SessionState a_state, const MenuChoice** a_outOpts, size_t* a_outCount)
{
    switch (a_state)
    {
        case SESSION_DISCONNECTED:
            *a_outOpts = s_disconnectedOptions;
            *a_outCount = sizeof(s_disconnectedOptions) / sizeof(s_disconnectedOptions[0]);
            return;
        case SESSION_CONNECTED:
            *a_outOpts = s_connectedOptions;
            *a_outCount = sizeof(s_connectedOptions) / sizeof(s_connectedOptions[0]);
            return;
        case SESSION_LOGGED_IN:
            *a_outOpts = s_loggedInOptions;
            *a_outCount = sizeof(s_loggedInOptions) / sizeof(s_loggedInOptions[0]);
            return;
    }
    *a_outOpts = NULL;
    *a_outCount = 0;
}

static const char* StateLabel(SessionState a_state)
{
    switch (a_state)
    {
        case SESSION_DISCONNECTED: return "DISCONNECTED";
        case SESSION_CONNECTED:    return "CONNECTED";
        case SESSION_LOGGED_IN:    return "LOGGED_IN";
    }
    return "?";
}

void Menu_Render(SessionState a_state)
{
    const MenuChoice* opts = NULL;
    size_t count = 0;
    GetOptions(a_state, &opts, &count);

    printf("\n=== Chat client [%s] ===\n", StateLabel(a_state));
    for (size_t i = 0; i < count; ++i)
    {
        printf("  %zu. %s\n", i + 1, ChoiceLabel(opts[i]));
    }
}

MenuChoice Menu_Parse(const char* a_line, SessionState a_state)
{
    if (a_line == NULL) return MENU_NONE;

    while (*a_line && isspace((unsigned char)*a_line)) ++a_line;
    if (*a_line == '\0') return MENU_NONE;

    if (!isdigit((unsigned char)*a_line)) return MENU_NONE;

    size_t selection = 0;
    while (isdigit((unsigned char)*a_line))
    {
        selection = selection * 10 + (size_t)(*a_line - '0');
        ++a_line;
    }
    while (*a_line && isspace((unsigned char)*a_line)) ++a_line;
    if (*a_line != '\0') return MENU_NONE;
    if (selection == 0) return MENU_NONE;

    const MenuChoice* opts = NULL;
    size_t count = 0;
    GetOptions(a_state, &opts, &count);
    if (selection > count) return MENU_NONE;

    return opts[selection - 1];
}

bool Menu_ReadLine(const char* a_prompt, char* a_buf, size_t a_size)
{
    if (a_buf == NULL || a_size == 0) return false;
    if (a_prompt != NULL)
    {
        printf("%s", a_prompt);
        fflush(stdout);
    }
    if (fgets(a_buf, (int)a_size, stdin) == NULL)
    {
        return false;
    }
    size_t len = strlen(a_buf);
    while (len > 0 && (a_buf[len - 1] == '\n' || a_buf[len - 1] == '\r'))
    {
        a_buf[--len] = '\0';
    }
    return true;
}
