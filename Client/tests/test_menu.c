#include <stdio.h>
#include <string.h>
#include "../Menu.h"

static int s_failed = 0;
static int s_passed = 0;

#define ASSERT_EQ(actual, expected, msg) do { \
    if ((actual) == (expected)) { \
        ++s_passed; \
    } else { \
        ++s_failed; \
        fprintf(stderr, "FAIL: %s: expected %d, got %d (%s:%d)\n", \
                (msg), (int)(expected), (int)(actual), __FILE__, __LINE__); \
    } \
} while (0)

static void test_disconnected_options(void)
{
    ASSERT_EQ(Menu_Parse("1", SESSION_DISCONNECTED), MENU_CONNECT, "DC:1 -> CONNECT");
    ASSERT_EQ(Menu_Parse("2", SESSION_DISCONNECTED), MENU_EXIT,    "DC:2 -> EXIT");
    ASSERT_EQ(Menu_Parse("3", SESSION_DISCONNECTED), MENU_NONE,    "DC:3 -> NONE (out of range)");
    ASSERT_EQ(Menu_Parse("0", SESSION_DISCONNECTED), MENU_NONE,    "DC:0 -> NONE");
}

static void test_connected_options(void)
{
    ASSERT_EQ(Menu_Parse("1", SESSION_CONNECTED), MENU_REGISTER, "C:1 -> REGISTER");
    ASSERT_EQ(Menu_Parse("2", SESSION_CONNECTED), MENU_LOGIN,    "C:2 -> LOGIN");
    ASSERT_EQ(Menu_Parse("3", SESSION_CONNECTED), MENU_EXIT,     "C:3 -> EXIT");
    ASSERT_EQ(Menu_Parse("4", SESSION_CONNECTED), MENU_NONE,     "C:4 -> NONE");
}

static void test_logged_in_options(void)
{
    ASSERT_EQ(Menu_Parse("1", SESSION_LOGGED_IN), MENU_LOGOUT,         "L:1 -> LOGOUT");
    ASSERT_EQ(Menu_Parse("2", SESSION_LOGGED_IN), MENU_CREATE_GROUP,   "L:2 -> CREATE");
    ASSERT_EQ(Menu_Parse("3", SESSION_LOGGED_IN), MENU_JOIN_GROUP,     "L:3 -> JOIN");
    ASSERT_EQ(Menu_Parse("4", SESSION_LOGGED_IN), MENU_LEAVE_GROUP,    "L:4 -> LEAVE");
    ASSERT_EQ(Menu_Parse("5", SESSION_LOGGED_IN), MENU_DISPLAY_USERS,  "L:5 -> DISPLAY_USERS");
    ASSERT_EQ(Menu_Parse("6", SESSION_LOGGED_IN), MENU_DISPLAY_GROUPS, "L:6 -> DISPLAY_GROUPS");
    ASSERT_EQ(Menu_Parse("7", SESSION_LOGGED_IN), MENU_EXIT,           "L:7 -> EXIT");
    ASSERT_EQ(Menu_Parse("8", SESSION_LOGGED_IN), MENU_NONE,           "L:8 -> NONE");
}

static void test_whitespace_and_junk(void)
{
    ASSERT_EQ(Menu_Parse("",      SESSION_DISCONNECTED), MENU_NONE, "empty -> NONE");
    ASSERT_EQ(Menu_Parse("   ",   SESSION_DISCONNECTED), MENU_NONE, "spaces -> NONE");
    ASSERT_EQ(Menu_Parse("abc",   SESSION_CONNECTED),    MENU_NONE, "letters -> NONE");
    ASSERT_EQ(Menu_Parse("1abc",  SESSION_CONNECTED),    MENU_NONE, "digit+letters -> NONE");
    ASSERT_EQ(Menu_Parse(" 1 ",   SESSION_CONNECTED),    MENU_REGISTER, "padded digit -> parsed");
    ASSERT_EQ(Menu_Parse("-1",    SESSION_CONNECTED),    MENU_NONE, "negative -> NONE");
    ASSERT_EQ(Menu_Parse(NULL,    SESSION_CONNECTED),    MENU_NONE, "NULL -> NONE");
}

static void test_exit_reachable_everywhere(void)
{
    ASSERT_EQ(Menu_Parse("2", SESSION_DISCONNECTED), MENU_EXIT, "EXIT from DC");
    ASSERT_EQ(Menu_Parse("3", SESSION_CONNECTED),    MENU_EXIT, "EXIT from C");
    ASSERT_EQ(Menu_Parse("7", SESSION_LOGGED_IN),    MENU_EXIT, "EXIT from L");
}

int main(void)
{
    test_disconnected_options();
    test_connected_options();
    test_logged_in_options();
    test_whitespace_and_junk();
    test_exit_reachable_everywhere();

    printf("\nMenu tests: %d passed, %d failed\n", s_passed, s_failed);
    return s_failed == 0 ? 0 : 1;
}
