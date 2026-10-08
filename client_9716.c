#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 15716
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024

int sock_fd;
volatile int running = 1;

void *receive_messages(void *arg)
{
    char buffer[BUFFER_SIZE];

    while (running)
    {
        memset(buffer, 0, sizeof(buffer));

        ssize_t bytes_received = recv(sock_fd,
                                      buffer,
                                      sizeof(buffer) - 1,
                                      0);

        if (bytes_received <= 0)
        {
            running = 0;
            break;
        }

        buffer[bytes_received] = '\0';

        printf("\nServer: %s", buffer);
        printf("> ");
        fflush(stdout);
    }

    return NULL;
}

int main(void)
{
    struct sockaddr_in server_addr;

    sock_fd = socket(AF_INET,
                     SOCK_STREAM,
                     0);

    if (sock_fd < 0)
    {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sock_fd);
        return 1;
    }

    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock_fd);
        return 1;
    }

    printf("Connected to NetMessenger server.\n");
    printf("Server: %s:%d\n",
           SERVER_IP,
           PORT);

    char buffer[BUFFER_SIZE];
    char username[50];

    printf("Enter username: ");

    if (scanf("%49s", username) != 1)
    {
        close(sock_fd);
        return 1;
    }

    getchar();

    snprintf(buffer,
             sizeof(buffer),
             "REGISTER %s\n",
             username);

    send(sock_fd,
         buffer,
         strlen(buffer),
         0);

    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received = recv(sock_fd,
                                  buffer,
                                  sizeof(buffer) - 1,
                                  0);

    if (bytes_received <= 0)
    {
        printf("Server disconnected.\n");
        close(sock_fd);
        return 1;
    }

    buffer[bytes_received] = '\0';

    printf("Server: %s", buffer);

    /* Start receiver thread */
    pthread_t receiver_thread;

    if (pthread_create(&receiver_thread,
                       NULL,
                       receive_messages,
                       NULL) != 0)
    {
        perror("pthread_create");
        close(sock_fd);
        return 1;
    }

    /* Command input */
    while (running)
    {
        printf("> ");
        fflush(stdout);

        if (fgets(buffer,
                  sizeof(buffer),
                  stdin) == NULL)
        {
            break;
        }

        if (send(sock_fd,
                 buffer,
                 strlen(buffer),
                 0) < 0)
        {
            perror("send");
            break;
        }

        if (strcmp(buffer, "QUIT\n") == 0)
        {
            sleep(1);
            running = 0;
            break;
        }
    }

    running = 0;

    shutdown(sock_fd, SHUT_RDWR);

    pthread_join(receiver_thread,
                 NULL);

    close(sock_fd);

    return 0;
}
