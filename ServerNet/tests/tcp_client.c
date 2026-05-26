#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>

#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 8080

int main(int argc, char **argv)
{
    char *server_ip = DEFAULT_SERVER_IP;
    uint16_t server_port = DEFAULT_SERVER_PORT;
    if (argc == 3)
    {
        server_ip = argv[1];
        server_port = atoi(argv[2]);
    }


    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        printf("Error: socket creation failed\n");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("Error: connection failed\n");
        return 1;
    }

    printf("Client: Connected to server\n");

    char buf[256];
    while (1)
    {
        printf("Enter message (or 'quit' to exit): ");
        if (fgets(buf, sizeof(buf), stdin) == NULL)
            break;

        if (strncmp(buf, "quit", 4) == 0)
            break;

        if (send(sockfd, buf, strlen(buf), 0) < 0)
        {
            printf("Error: send failed\n");
            break;
        }
    }

    close(sockfd);
    return 0;
}