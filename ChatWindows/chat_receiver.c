/*
 * chat_receiver — standalone program spawned by the main client in its own
 * terminal window. It joins a group's UDP multicast endpoint and prints every
 * incoming datagram to stdout.
 *
 * On startup it reports its own PID to the client over a POSIX message queue so
 * the client can kill() it (closing the window) when the user leaves the group.
 *
 * Usage: chat_receiver <multicast_ip> <port>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>     /* close */
#include <sys/socket.h>
#include <netinet/in.h> /* struct sockaddr_in, ip_mreq, INADDR_ANY */
#include <arpa/inet.h>  /* inet_addr */

#include "config.h"     /* CONF_CHAT_MSG_MAX */
#include "ChatIpc.h"    /* ChatIpc_ReportPid, CHAT_ROLE_RECEIVER */

/* Create a UDP socket, bind it to the multicast port, and join the multicast
 * group at a_ip. Returns the socket fd, or -1 on error. */
static int CreateAndJoinMulticast(const char* a_ip, uint16_t a_port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        perror("chat_receiver: socket");
        return -1;
    }

    /* Allow several receivers to bind the same port (e.g. multiple clients on
     * one host during testing). Must be set before bind(). */
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        perror("chat_receiver: SO_REUSEADDR");
        close(fd);
        return -1;
    }

    /* Bind to the group's port on any local interface. */
    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(a_port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("chat_receiver: bind");
        close(fd);
        return -1;
    }

    /* Ask the kernel to join the multicast group on the default interface, so
     * datagrams sent to a_ip:a_port are delivered to this socket. */
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(a_ip);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        perror("chat_receiver: IP_ADD_MEMBERSHIP");
        close(fd);
        return -1;
    }

    return fd;
}

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <multicast_ip> <port>\n", argv[0]);
        return 1;
    }
    const char* ip   = argv[1];
    uint16_t    port = (uint16_t)atoi(argv[2]);

    ChatIpc_ReportPid(CHAT_ROLE_RECEIVER);

    int fd = CreateAndJoinMulticast(ip, port);
    if (fd < 0)
    {
        return 1;
    }

    printf("Listening on %s:%u  (close this window to stop)\n", ip, (unsigned)port);
    fflush(stdout);

    /* Print every datagram until recvfrom fails (e.g. the client kills us). */
    char buf[CONF_CHAT_MSG_MAX];
    for (;;)
    {
        ssize_t received = recvfrom(fd, buf, sizeof(buf) - 1, 0, NULL, NULL);
        if (received < 0)
        {
            perror("chat_receiver: recvfrom");
            break;
        }
        buf[received] = '\0';   /* datagram carries "username: text" */
        printf("%s\n", buf);
        fflush(stdout);
    }

    close(fd);
    return 0;
}
