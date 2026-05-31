#ifndef __GROUP_WINDOWS_H__
#define __GROUP_WINDOWS_H__

#include <stdint.h>  /* uint16_t */

/*
 * GroupWindows manages the per-group chat terminal windows on the client side.
 *
 * For each group the user is in, two helper programs run in their own
 * gnome-terminal windows: chat_sender and chat_receiver. This module:
 *   - owns the POSIX message queue the spawned programs use to report their PIDs,
 *   - spawns the two windows for a group and collects their PIDs,
 *   - tracks {group name -> sender/receiver PID} so the windows can be closed
 *     (via kill()) when the user leaves the group, logs out, or exits.
 *
 * Single-threaded: all calls happen on the client's main menu thread.
 */

typedef struct GroupWindows GroupWindows;

typedef enum GroupWindowsResult
{
    GROUP_WINDOWS_SUCCESS = 0,
    GROUP_WINDOWS_NULL_PTR,
    GROUP_WINDOWS_ALLOC_FAILED,
    GROUP_WINDOWS_SPAWN_FAILED,   /* system()/gnome-terminal failed to launch */
    GROUP_WINDOWS_PID_TIMEOUT,    /* a window did not report its PID in time */
    GROUP_WINDOWS_NOT_FOUND,      /* no windows tracked for that group */
} GroupWindowsResult;

/**
 * @brief Create the manager and its PID message queue.
 * @return a new GroupWindows, or NULL on allocation / queue-creation failure.
 */
GroupWindows* GroupWindows_Create(void);

/**
 * @brief Close all remaining windows, tear down the queue, and free the manager.
 *        On return *a_self is NULL. Safe to call with NULL or *a_self == NULL.
 */
void GroupWindows_Destroy(GroupWindows** a_self);

/**
 * @brief Spawn a sender + receiver window for a group and record their PIDs.
 * @details Launches both helper programs pointed at a_ip:a_port (sender also
 *          gets a_username), then waits for each to report its PID over the
 *          queue. If the group already has windows, the old pair is closed
 *          first. PIDs are stored keyed by a_groupName.
 * @return GROUP_WINDOWS_SUCCESS, or _NULL_PTR / _ALLOC_FAILED / _SPAWN_FAILED /
 *         _PID_TIMEOUT on failure.
 */
GroupWindowsResult GroupWindows_Open(GroupWindows* a_self,
                                     const char* a_groupName,
                                     const char* a_ip,
                                     uint16_t a_port,
                                     const char* a_username);

/**
 * @brief Close a group's windows (kill its sender + receiver) and stop tracking it.
 * @return GROUP_WINDOWS_SUCCESS, _NULL_PTR, or _NOT_FOUND if the group is untracked.
 */
GroupWindowsResult GroupWindows_Close(GroupWindows* a_self, const char* a_groupName);

/**
 * @brief Close every tracked group's windows (used on logout / exit).
 */
void GroupWindows_CloseAll(GroupWindows* a_self);

#endif /* __GROUP_WINDOWS_H__ */
