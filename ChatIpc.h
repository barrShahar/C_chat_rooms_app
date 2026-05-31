#ifndef __CHAT_IPC_H__
#define __CHAT_IPC_H__

#include <sys/types.h>   /* pid_t */

/*
 * Shared type contract between the main client and the spawned chat_sender /
 * chat_receiver programs.
 *
 * Each spawned program reports its own getpid() back to the client over a
 * POSIX message queue (CONF_CHAT_PID_QUEUE_NAME, in config.h) so the client can
 * later kill() it to close the window. The role field tells the client which
 * PID is the sender and which is the receiver (POSIX queues have no System V
 * style mtype selector).
 */

typedef enum
{
    CHAT_ROLE_SENDER   = 0,
    CHAT_ROLE_RECEIVER = 1,
} ChatRole;

typedef struct
{
    int   m_role;   /* ChatRole */
    pid_t m_pid;    /* getpid() of the reporting program */
} ChatPidMsg;

/* Report this process's PID (and role) to the main client over the POSIX
 * message queue. Best-effort: failures are logged via perror and ignored, so a
 * spawned chat window still works even if the client can't auto-close it later.
 * Called by chat_sender and chat_receiver at startup. */
void ChatIpc_ReportPid(ChatRole a_role);

#endif /* __CHAT_IPC_H__ */
