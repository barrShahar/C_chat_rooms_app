/*
 * chat_sender — standalone program spawned by the main client in its own
 * terminal window. It reads lines from the keyboard and broadcasts each one to
 * a group's UDP multicast endpoint, tagged with the user's name.
 *
 * On startup it reports its own PID to the client over a POSIX message queue so
 * the client can kill() it (closing the window) when the user leaves the group.
 *
 * Usage: chat_sender <multicast_ip> <port> <username>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>     /* memset, strcspn */
#include <unistd.h>     /* close */
#include <sys/socket.h>
#include <netinet/in.h> /* struct sockaddr_in */
#include <arpa/inet.h>  /* inet_addr */

#include "config.h"     /* CONF_CHAT_MSG_MAX */
#include "ChatIpc.h"    /* ChatIpc_ReportPid, CHAT_ROLE_SENDER */

/* Create a UDP socket for sending and fill a_outDest with the multicast
 * destination a_ip:a_port. Returns the socket fd, or -1 on error. */
static int CreateMulticastSender(const char* a_ip, uint16_t a_port, struct sockaddr_in* a_outDest)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        perror("chat_sender: socket");
        return -1;
    }

    /* No bind/join needed to send; sending to a multicast address is the same
     * as sending to any UDP address. */
    memset(a_outDest, 0, sizeof(*a_outDest));
    a_outDest->sin_family      = AF_INET;
    a_outDest->sin_addr.s_addr = inet_addr(a_ip);
    a_outDest->sin_port        = htons(a_port);
    return fd;
}

int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s <multicast_ip> <port> <username>\n", argv[0]);
        return 1;
    }
    const char* ip       = argv[1];
    uint16_t    port     = (uint16_t)atoi(argv[2]);
    const char* username = argv[3];

    ChatIpc_ReportPid(CHAT_ROLE_SENDER);

    struct sockaddr_in dest;
    int fd = CreateMulticastSender(ip, port, &dest);
    if (fd < 0)
    {
        return 1;
    }

    printf("Connected to %s:%u as '%s'. Type a message and press Enter "
           "(close this window to stop).\n", ip, (unsigned)port, username);
    fflush(stdout);

    char line[CONF_CHAT_MSG_MAX];
    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        line[strcspn(line, "\n")] = '\0';   /* strip the trailing newline */
        if (line[0] == '\0')
        {
            continue;                       /* ignore empty lines */
        }

        /* Tag the message with the sender's name; snprintf truncates to fit. */
        char msg[CONF_CHAT_MSG_MAX];
        int written = snprintf(msg, sizeof(msg), "%s: %s", username, line);
        if (written < 0)
        {
            continue;
        }
        size_t toSend = (written >= (int)sizeof(msg)) ? sizeof(msg) - 1 : (size_t)written;

        if (sendto(fd, msg, toSend, 0, (struct sockaddr*)&dest, sizeof(dest)) < 0)
        {
            perror("chat_sender: sendto");
            break;
        }
    }

    close(fd);
    return 0;
}
