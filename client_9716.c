#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>

#define PORT 15716
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024
#define FILE_BUFFER_SIZE 4096

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

int send_file(const char *target,
              const char *filepath)
{
    FILE *file = fopen(filepath, "rb");

    if (file == NULL)
    {
        perror("fopen");
        return -1;
    }

    struct stat file_info;

    if (stat(filepath, &file_info) != 0)
    {
        perror("stat");
        fclose(file);
        return -1;
    }

    long long filesize = file_info.st_size;

    const char *filename = strrchr(filepath, '/');

    if (filename != NULL)
        filename++;
    else
        filename = filepath;

    char header[BUFFER_SIZE];

    snprintf(header,
             sizeof(header),
             "SENDFILE %s %s %lld\n",
             target,
             filename,
             filesize);

    if (send(sock_fd,
             header,
             strlen(header),
             0) < 0)
    {
        perror("send");
        fclose(file);
        return -1;
    }

    char file_buffer[FILE_BUFFER_SIZE];

    long long sent_total = 0;

    while (sent_total < filesize)
    {
        size_t bytes_to_read =
            (filesize - sent_total < FILE_BUFFER_SIZE)
                ? (size_t)(filesize - sent_total)
                : FILE_BUFFER_SIZE;

        size_t bytes_read =
            fread(file_buffer,
                  1,
                  bytes_to_read,
                  file);

        if (bytes_read == 0)
            break;

        size_t total_sent = 0;

        while (total_sent < bytes_read)
        {
            ssize_t sent =
                send(sock_fd,
                     file_buffer + total_sent,
                     bytes_read - total_sent,
                     0);

            if (sent <= 0)
            {
                perror("send");
                fclose(file);
                return -1;
            }

            total_sent += sent;
        }

        sent_total += bytes_read;
    }

    fclose(file);

    if (sent_total != filesize)
    {
        printf("File transfer incomplete.\n");
        return -1;
    }

    printf("File sent: %s (%lld bytes)\n",
           filename,
           filesize);

    return 0;
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

    ssize_t bytes_received =
        recv(sock_fd,
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

        if (strncmp(buffer,
                    "SENDFILE ",
                    9) == 0)
        {
            char target[50];
            char filepath[500];

            if (sscanf(buffer + 9,
                       "%49s %499s",
                       target,
                       filepath) != 2)
            {
                printf("Usage: SENDFILE <target> <filepath>\n");
                continue;
            }

            send_file(target,
                      filepath);

            continue;
        }

        if (send(sock_fd,
                 buffer,
                 strlen(buffer),
                 0) < 0)
        {
            perror("send");
            break;
        }

        if (strcmp(buffer,
                   "QUIT\n") == 0)
        {
            sleep(1);
            running = 0;
            break;
        }
    }

    running = 0;

    shutdown(sock_fd,
             SHUT_RDWR);

    pthread_join(receiver_thread,
                 NULL);

    close(sock_fd);

    return 0;
}
