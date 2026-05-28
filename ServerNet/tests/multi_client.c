#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include "logger.h"

#define NUMBER_OF_CLIENTS 20
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8080
#define BUF_SIZE 1024
#define MAX_EMPTY_LOOPS 3

#define IS_DISCONNECTED(num) (num == 0)

static void Die(const char *msg);
static int ClientConnect(int i);
static int ClientDisconnect(int fd, int clientNumber);
static int ClientSayHello(int fd, int clientNumber);


int main(void)
{

    int* clients = calloc(NUMBER_OF_CLIENTS, sizeof(int));

    long loopNum = 0;
    int emptyLoops = 0;
    while (true)
    {
        int activeOrConnected = 0;
        for (int i = 0 ; i < NUMBER_OF_CLIENTS ; ++i)
        {
            if (IS_DISCONNECTED(clients[i]))
            {
                if (((double)rand() / RAND_MAX) > 0.3)   // 30% to connect
                {
                    clients[i] = ClientConnect(i);
                    if (clients[i]) activeOrConnected++;
                }
            }
            else
            {
                activeOrConnected++;
                if (((double)rand() / RAND_MAX) < 0.05)
                {
                    clients[i] = ClientDisconnect(clients[i], i);
                }
                else if (((double)rand() / RAND_MAX) < 0.3)
                {
                    clients[i] = ClientSayHello(clients[i], i);
                }
            }
        }

        if (activeOrConnected == 0)
        {
            if (++emptyLoops >= MAX_EMPTY_LOOPS)
            {
                LOG_INFO("server appears down, exiting after %d empty loops", emptyLoops);
                break;
            }
        }
        else
        {
            emptyLoops = 0;
        }

        LOG_INFO("loop num: %ld", loopNum++);
        sleep(1);
    }


    
    return 0;
}

static int ClientConnect(int i)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        Die("socket");
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)DEFAULT_PORT);

    if (inet_pton(AF_INET, DEFAULT_HOST, &addr.sin_addr) <= 0) {
        LOG_ERROR("Invalid address: %s", DEFAULT_HOST);
        close(fd);
        return 0;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("connect failed: %s", strerror(errno));
        close(fd);
        return 0;
    }

    LOG_INFO("Connected client: %d", i);
    return fd;
}

static int ClientSayHello(int fd, int clientNumber)
{
    char message[50];
    snprintf(message, sizeof(message), "Hello from Client %d", clientNumber);

    size_t msg_len = strlen(message);
    if (send(fd, message, msg_len, 0) < 0)
    {
        Die("send");
    }

    LOG_DEBUG("Sent: %s", message);

    char buf[BUF_SIZE];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n < 0) {
        Die("recv");
    }
    if (n == 0) {
        LOG_INFO("Client %d: server closed connection", clientNumber);
        close(fd);
        return 0;
    }
    buf[n] = '\0';
    LOG_DEBUG("Client %d received: %s", clientNumber, buf);

    return fd;
}

static int ClientDisconnect(int fd, int clientNumber)
{
    if (shutdown(fd, SHUT_WR) < 0)
    {
        perror("Shutdown failed");
        exit(1);
    }

    close(fd);
    LOG_INFO("Disconnected client %d", clientNumber);
    return 0;
}


static void Die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}