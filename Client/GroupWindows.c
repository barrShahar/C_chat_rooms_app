#include <stdlib.h>     /* malloc, free, system */
#include <stdio.h>      /* snprintf */
#include <string.h>     /* strcmp, strdup, strerror */
#include <errno.h>
#include <signal.h>     /* kill, SIGTERM */
#include <time.h>       /* clock_gettime, struct timespec */
#include <fcntl.h>      /* O_CREAT, O_RDONLY */
#include <mqueue.h>     /* POSIX message queue */

#include "GroupWindows.h"
#include "ChatIpc.h"    /* ChatPidMsg, CHAT_ROLE_* */
#include "config.h"     /* CONF_CHAT_* */
#include "HashMap.h"
#include "logger.h"

/* Paths to the spawned helper binaries, relative to the client's working
 * directory (run from the repo root, like build/out.client). */
#define CHAT_RECEIVER_BIN "build/out.chat_receiver"
#define CHAT_SENDER_BIN   "build/out.chat_sender"

/* The two window PIDs tracked per group; stored as the HashMap value. */
typedef struct
{
    pid_t m_sender;
    pid_t m_receiver;
} GroupPids;

struct GroupWindows
{
    HashMap* m_groups;   /* strdup'd group name -> GroupPids* */
    mqd_t    m_queue;    /* PID-report queue the spawned windows write to */
};

/* ---- HashMap callbacks (string keys, GroupPids values) ------------------- */

/* DJB2 string hash function */
static size_t HashGroupName(const void* a_key)
{
    const char* str = (const char*)a_key;
    size_t hash = 5381;
    int c;
    while ((c = *str++))
    {
        hash = ((hash << 5) + hash) + (size_t)c;  /* hash * 33 + c */
    }
    return hash;
}

/* Returns non-zero when the two group names are equal (HashMap convention). */
static int GroupNamesEqual(const void* a_first, const void* a_second)
{
    return strcmp((const char*)a_first, (const char*)a_second) == 0;
}

/* -------------------------------------------------------------------------- */

GroupWindows* GroupWindows_Create(void)
{
    /* Allocate memory for the GroupWindows struct */
    GroupWindows* self = (GroupWindows*)malloc(sizeof(GroupWindows));
    if (self == NULL)
    {
        LOG_ERROR("GroupWindows_Create: allocation failed");
        return NULL;
    }

    /* Create a hash map to store the group names and their corresponding PIDs */
    self->m_groups = HashMap_Create(CONF_CHAT_WINDOWS_MAP_SIZE, HashGroupName, GroupNamesEqual);
    if (self->m_groups == NULL)
    {
        LOG_ERROR("GroupWindows_Create: failed to create hash map");
        free(self);
        return NULL;
    }

    /* Drop any stale queue from a previous (possibly crashed) run so our own
     * attributes take effect, then create it fresh. The spawned windows open it
     * O_WRONLY without O_CREAT, so it must exist before any window is launched. */
    mq_unlink(CONF_CHAT_PID_QUEUE_NAME);

    struct mq_attr attr = {0};
    attr.mq_maxmsg  = CONF_CHAT_PID_QUEUE_MAXMSG;
    attr.mq_msgsize = sizeof(ChatPidMsg);

    self->m_queue = mq_open(CONF_CHAT_PID_QUEUE_NAME, O_CREAT | O_RDONLY, 0600, &attr);
    if (self->m_queue == (mqd_t)-1)
    {
        LOG_ERROR("GroupWindows_Create: mq_open failed: %s", strerror(errno));
        HashMap_Destroy(&self->m_groups, NULL, NULL);
        free(self);
        return NULL;
    }

    return self;
}

/* Launch a_command in its own gnome-terminal window via system().
 * Detects only whether the terminal itself could be launched (e.g. it returns
 * non-zero / 127 when gnome-terminal is missing). Whether the helper program
 * inside actually started is confirmed separately by CollectPid. */
static GroupWindowsResult SpawnWindow(const char* a_command)
{
    LOG_DEBUG("GroupWindows: spawning window: %s", a_command);
    int return_code = system(a_command);
    if (return_code != 0)
    {
        LOG_ERROR("GroupWindows: failed to spawn window (system returned %d): %s", return_code, a_command);
        return GROUP_WINDOWS_SPAWN_FAILED;
    }
    return GROUP_WINDOWS_SUCCESS;
}

/* Wait up to CONF_CHAT_PID_WAIT_SECONDS for one window to report its PID over
 * the queue, then store that PID into a_pids according to its role. Returns
 * GROUP_WINDOWS_PID_TIMEOUT if no message arrives in time. */
static GroupWindowsResult CollectPid(GroupWindows* a_self, GroupPids* a_pids)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += CONF_CHAT_PID_WAIT_SECONDS;

    ChatPidMsg msg;
    ssize_t received = mq_timedreceive(a_self->m_queue, (char*)&msg, sizeof(msg), NULL, &deadline);
    if (received < 0)
    {
        LOG_ERROR("GroupWindows: timed out waiting for a window PID: %s", strerror(errno));
        return GROUP_WINDOWS_PID_TIMEOUT;
    }

    if (msg.m_role == CHAT_ROLE_SENDER)
    {
        a_pids->m_sender = msg.m_pid;
    }
    else
    {
        a_pids->m_receiver = msg.m_pid;
    }
    return GROUP_WINDOWS_SUCCESS;
}

/* SIGTERM any set (non-zero) PID, closing its window. ESRCH (the window was
 * already closed manually) is expected and ignored. */
static void KillPids(const GroupPids* a_pids)
{
    if (a_pids->m_sender > 0 && kill(a_pids->m_sender, SIGTERM) < 0 && errno != ESRCH)
    {
        LOG_WARN("GroupWindows: failed to kill sender %d: %s", a_pids->m_sender, strerror(errno));
    }
    if (a_pids->m_receiver > 0 && kill(a_pids->m_receiver, SIGTERM) < 0 && errno != ESRCH)
    {
        LOG_WARN("GroupWindows: failed to kill receiver %d: %s", a_pids->m_receiver, strerror(errno));
    }
}

GroupWindowsResult GroupWindows_Open(GroupWindows* a_self, const char* a_groupName,
                                     const char* a_ip, uint16_t a_port, const char* a_username)
{
    if (a_self == NULL || a_groupName == NULL || a_ip == NULL || a_username == NULL)
    {
        return GROUP_WINDOWS_NULL_PTR;
    }

    /* Re-opening a group: close the stale window pair first so it isn't leaked. */
    if (HashMap_Find(a_self->m_groups, a_groupName, NULL) == MAP_SUCCESS)
    {
        GroupWindows_Close(a_self, a_groupName);
    }

    /* Launch the receiver window, then the sender window. */
    char cmd[CONF_CHAT_SPAWN_CMD_MAX];

    snprintf(cmd, sizeof(cmd), "gnome-terminal -- %s %s %u",
             CHAT_RECEIVER_BIN, a_ip, (unsigned)a_port);
    GroupWindowsResult result = SpawnWindow(cmd);
    if (result != GROUP_WINDOWS_SUCCESS)
    {
        return result;
    }

    snprintf(cmd, sizeof(cmd), "gnome-terminal -- %s %s %u %s",
             CHAT_SENDER_BIN, a_ip, (unsigned)a_port, a_username);
    result = SpawnWindow(cmd);
    if (result != GROUP_WINDOWS_SUCCESS)
    {
        return result;   /* the receiver window may linger; user can close it */
    }

    /* Collect both PIDs. calloc zeroes the pair so CollectPid can fill it by
     * role in either arrival order, and KillPids skips the unset (0) slot. */
    GroupPids* pids = (GroupPids*)calloc(1, sizeof(GroupPids));
    if (pids == NULL)
    {
        LOG_ERROR("GroupWindows_Open: allocation failed");
        return GROUP_WINDOWS_ALLOC_FAILED;
    }

    for (int i = 0; i < 2; ++i)
    {
        result = CollectPid(a_self, pids);
        if (result != GROUP_WINDOWS_SUCCESS)
        {
            KillPids(pids);   /* tear down whichever window did report */
            free(pids);
            return result;
        }
    }

    /* Track the pair under a private copy of the group name. */
    char* key = strdup(a_groupName);
    if (key == NULL)
    {
        LOG_ERROR("GroupWindows_Open: strdup failed");
        KillPids(pids);
        free(pids);
        return GROUP_WINDOWS_ALLOC_FAILED;
    }

    if (HashMap_Insert(a_self->m_groups, key, pids) != MAP_SUCCESS)
    {
        LOG_ERROR("GroupWindows_Open: failed to track group %s", a_groupName);
        KillPids(pids);
        free(pids);
        free(key);
        return GROUP_WINDOWS_ALLOC_FAILED;
    }

    LOG_INFO("GroupWindows: opened windows for group %s (sender=%d, receiver=%d)",
             a_groupName, pids->m_sender, pids->m_receiver);
    return GROUP_WINDOWS_SUCCESS;
}

GroupWindowsResult GroupWindows_Close(GroupWindows* a_self, const char* a_groupName)
{
    if (a_self == NULL || a_groupName == NULL)
    {
        return GROUP_WINDOWS_NULL_PTR;
    }

    void* removedKey   = NULL;
    void* removedValue = NULL;
    if (HashMap_Remove(a_self->m_groups, a_groupName, &removedKey, &removedValue) != MAP_SUCCESS)
    {
        return GROUP_WINDOWS_NOT_FOUND;
    }

    KillPids((const GroupPids*)removedValue);
    LOG_INFO("GroupWindows: closed windows for group %s", a_groupName);

    free(removedKey);
    free(removedValue);
    return GROUP_WINDOWS_SUCCESS;
}

/* HashMap_ForEach callback: SIGTERM a group's window pair. Returns 1 to keep
 * iterating over the remaining groups. */
static int KillPidsForEach(const void* a_key, void* a_value, void* a_context)
{
    (void)a_key; (void)a_context;
    KillPids((const GroupPids*)a_value);
    return 1;
}

void GroupWindows_CloseAll(GroupWindows* a_self)
{
    if (a_self == NULL)
    {
        return;
    }

    /* Kill every window, then clear the map (freeing keys + values) while
     * keeping an empty, usable map for any later joins (e.g. after logout). */
    HashMap_ForEach(a_self->m_groups, KillPidsForEach, NULL);
    HashMap_Destroy(&a_self->m_groups, free, free);
    a_self->m_groups = HashMap_Create(CONF_CHAT_WINDOWS_MAP_SIZE, HashGroupName, GroupNamesEqual);
    if (a_self->m_groups == NULL)
    {
        LOG_ERROR("GroupWindows_CloseAll: failed to recreate group map");
    }
}

void GroupWindows_Destroy(GroupWindows** a_self)
{
    if (a_self == NULL || *a_self == NULL)
    {
        return;
    }

    GroupWindows* self = *a_self;

    /* Close any windows still open, then free the map. */
    if (self->m_groups != NULL)
    {
        HashMap_ForEach(self->m_groups, KillPidsForEach, NULL);
        HashMap_Destroy(&self->m_groups, free, free);
    }

    /* Tear down the PID queue so it does not linger in /dev/mqueue. */
    if (self->m_queue != (mqd_t)-1)
    {
        mq_close(self->m_queue);
    }
    mq_unlink(CONF_CHAT_PID_QUEUE_NAME);

    free(self);
    *a_self = NULL;
}
