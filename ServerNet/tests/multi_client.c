#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <unistd.h>

#define NUMBER_OF_CLIENTS 20
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8080
#define BUF_SIZE 1024

#define IS_DISCONNECTED(num) (num == 0)

static void Die(const char *msg);
static int ClientConnect(int i);
static int ClientDisconnect(int fd, int clientNumber);
static int ClientSayHello(int fd, int clientNumber);


int main(void)
{

    int* clients = calloc(NUMBER_OF_CLIENTS, sizeof(int));

    long loopNum = 0;
    while (true)
    {
        for (int i = 0 ; i < NUMBER_OF_CLIENTS ; ++i)
        {
            if (IS_DISCONNECTED(clients[i]))
            {
                if (((double)rand() / RAND_MAX) > 0.3)   // 30% to connect
                {
                    clients[i] = ClientConnect(i);
                }
            }
            else
            {
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
        printf("loop num: %ld\n", loopNum++);
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
        fprintf(stderr, "Invalid address: %s\n", DEFAULT_HOST);
        close(fd);
        return 0;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return 0;
    }

    printf("Connected client: %d\n", i);
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

    printf("Sent: %s", message);

    // char buf[BUF_SIZE];
    // ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    // if (n < 0) {
    //     Die("recv");
    // }
    // if (n == 0) {
    //     printf("Server closed connection without reply\n");
    // } else {
    //     buf[n] = '\0';
    //     printf("Reply: %s", buf);
    // }

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
    printf("Disconnected client %d", clientNumber);
    return 0;
}


static void Die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}