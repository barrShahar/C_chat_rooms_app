#include <stdio.h>
#include <unistd.h>     /* getpid */
#include <fcntl.h>      /* O_WRONLY */
#include <mqueue.h>     /* POSIX message queue */

#include "ChatIpc.h"
#include "config.h"     /* CONF_CHAT_PID_QUEUE_NAME */

void ChatIpc_ReportPid(ChatRole a_role)
{
    mqd_t mq = mq_open(CONF_CHAT_PID_QUEUE_NAME, O_WRONLY);
    if (mq == (mqd_t)-1)
    {
        perror("ChatIpc_ReportPid: mq_open");
        return;
    }

    ChatPidMsg msg = { .m_role = a_role, .m_pid = getpid() };
    if (mq_send(mq, (const char*)&msg, sizeof(msg), 0) == -1)
    {
        perror("ChatIpc_ReportPid: mq_send");
    }

    mq_close(mq);
}
