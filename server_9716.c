#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 15716
#define BACKLOG 10
#define BUFFER_SIZE 1024
#define NID "7797"
#define MAX_CLIENTS 20
#define USERNAME_SIZE 50

typedef struct
{
    int socket;
    char username[USERNAME_SIZE];
    int registered;
} Client;

Client clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

int find_free_slot(void)
{
    int i;

    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].socket == 0)
        {
            return i;
        }
    }

    return -1;
}

void *handle_client(void *arg)
{
    int index = *(int *)arg;
    free(arg);

    int client_fd = clients[index].socket;
    char buffer[BUFFER_SIZE];

    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received = recv(client_fd,
                                  buffer,
                                  sizeof(buffer) - 1,
                                  0);

    if (bytes_received <= 0)
    {
        close(client_fd);

        pthread_mutex_lock(&clients_mutex);

        clients[index].socket = 0;
        clients[index].registered = 0;
        clients[index].username[0] = '\0';

        pthread_mutex_unlock(&clients_mutex);

        return NULL;
    }

    buffer[bytes_received] = '\0';

    printf("Received from client %d: %s",
           index + 1,
           buffer);

    char username[USERNAME_SIZE];

    if (sscanf(buffer, "REGISTER %49s", username) == 1)
    {
        int duplicate = 0;

        pthread_mutex_lock(&clients_mutex);

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].registered &&
                strcmp(clients[i].username, username) == 0)
            {
                duplicate = 1;
                break;
            }
        }

        if (!duplicate)
        {
            strncpy(clients[index].username,
                    username,
                    USERNAME_SIZE - 1);

            clients[index].username[USERNAME_SIZE - 1] = '\0';
            clients[index].registered = 1;
        }

        pthread_mutex_unlock(&clients_mutex);

        char response[128];

        if (duplicate)
        {
            snprintf(response,
                     sizeof(response),
                     "ERR 001 USERNAME_TAKEN NID:%s\n",
                     NID);
        }
        else
        {
            snprintf(response,
                     sizeof(response),
                     "OK REGISTERED NID:%s\n",
                     NID);

            printf("User registered: %s\n",
                   username);
        }

        send(client_fd,
             response,
             strlen(response),
             0);
    }
    else
    {
        char response[128];

        snprintf(response,
                 sizeof(response),
                 "ERR 006 INVALID_COMMAND NID:%s\n",
                 NID);

        send(client_fd,
             response,
             strlen(response),
             0);
    }

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        bytes_received = recv(client_fd,
                              buffer,
                              sizeof(buffer) - 1,
                              0);

        if (bytes_received <= 0)
        {
            break;
        }

        buffer[bytes_received] = '\0';

        printf("[%s] %s",
               clients[index].registered
                   ? clients[index].username
                   : "UNKNOWN",
               buffer);

        if (strncmp(buffer, "LIST", 4) == 0)
        {
            char user_list[BUFFER_SIZE];
            char response[BUFFER_SIZE];

            user_list[0] = '\0';

            pthread_mutex_lock(&clients_mutex);

            int first_user = 1;

            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i].registered)
                {
                    if (!first_user)
                    {
                        strncat(user_list,
                                ",",
                                sizeof(user_list) -
                                    strlen(user_list) - 1);
                    }

                    strncat(user_list,
                            clients[i].username,
                            sizeof(user_list) -
                                strlen(user_list) - 1);

                    first_user = 0;
                }
            }

            pthread_mutex_unlock(&clients_mutex);

            snprintf(response,
                     sizeof(response),
                     "OK USERS %s NID:%s\n",
                     user_list,
                     NID);

            ssize_t sent = send(client_fd,
                                response,
                                strlen(response),
                                0);

            printf("LIST response sent: %zd bytes: %s",
                   sent,
                   response);
        }
    }

    close(client_fd);

    pthread_mutex_lock(&clients_mutex);

    printf("Client disconnected: %s\n",
           clients[index].registered
               ? clients[index].username
               : "UNKNOWN");

    clients[index].socket = 0;
    clients[index].registered = 0;
    clients[index].username[0] = '\0';

    pthread_mutex_unlock(&clients_mutex);

    return NULL;
}

int main(void)
{
    int server_fd;
    struct sockaddr_in server_addr;

    memset(clients, 0, sizeof(clients));

    server_fd = socket(AF_INET,
                       SOCK_STREAM,
                       0);

    if (server_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;

    if (setsockopt(server_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, BACKLOG) < 0)
    {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("NetMessenger server started.\n");
    printf("Listening on TCP port %d\n", PORT);
    printf("Waiting for clients...\n");

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);

        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        pthread_mutex_lock(&clients_mutex);

        int index = find_free_slot();

        if (index == -1)
        {
            pthread_mutex_unlock(&clients_mutex);

            char response[128];

            snprintf(response,
                     sizeof(response),
                     "ERR 008 SERVER_FULL NID:%s\n",
                     NID);

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            close(client_fd);

            continue;
        }

        clients[index].socket = client_fd;
        clients[index].registered = 0;
        clients[index].username[0] = '\0';

        pthread_mutex_unlock(&clients_mutex);

        printf("Client connected from %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        int *thread_index = malloc(sizeof(int));

        if (thread_index == NULL)
        {
            perror("malloc");

            close(client_fd);

            pthread_mutex_lock(&clients_mutex);

            clients[index].socket = 0;

            pthread_mutex_unlock(&clients_mutex);

            continue;
        }

        *thread_index = index;

        pthread_t thread_id;

        if (pthread_create(&thread_id,
                           NULL,
                           handle_client,
                           thread_index) != 0)
        {
            perror("pthread_create");

            free(thread_index);
            close(client_fd);

            pthread_mutex_lock(&clients_mutex);

            clients[index].socket = 0;

            pthread_mutex_unlock(&clients_mutex);

            continue;
        }

        pthread_detach(thread_id);
    }

    close(server_fd);

    return 0;
}
