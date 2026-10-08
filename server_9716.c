#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <errno.h>

#define PORT 15716
#define BACKLOG 10
#define BUFFER_SIZE 1024
#define FILE_BUFFER_SIZE 4096
#define NID "7797"
#define MAX_CLIENTS 20
#define MAX_ROOMS 10
#define ROOM_NAME_SIZE 50
#define USERNAME_SIZE 50
#define STORAGE_PATH "./storage/IT23779716"

typedef struct
{
    int socket;
    char username[USERNAME_SIZE];
    int registered;
    int room_index;
} Client;

typedef struct
{
    char name[ROOM_NAME_SIZE];
    int active;
} Room;

Client clients[MAX_CLIENTS];
Room rooms[MAX_ROOMS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

int find_free_slot(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].socket == 0)
            return i;
    }

    return -1;
}

int find_room(const char *name)
{
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (rooms[i].active &&
            strcmp(rooms[i].name, name) == 0)
            return i;
    }

    return -1;
}

int find_free_room(void)
{
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (!rooms[i].active)
            return i;
    }

    return -1;
}

void send_response(int client_fd, const char *message)
{
    char response[BUFFER_SIZE];

    snprintf(response,
             sizeof(response),
             "%s NID:%s\n",
             message,
             NID);

    send(client_fd,
         response,
         strlen(response),
         0);
}

int receive_exact_bytes(int fd, FILE *file, long long size)
{
    char buffer[FILE_BUFFER_SIZE];
    long long received = 0;

    while (received < size)
    {
        long long remaining = size - received;
        size_t chunk = remaining < FILE_BUFFER_SIZE
                           ? (size_t)remaining
                           : FILE_BUFFER_SIZE;

        ssize_t bytes = recv(fd,
                             buffer,
                             chunk,
                             0);

        if (bytes <= 0)
            return -1;

        fwrite(buffer,
               1,
               bytes,
               file);

        received += bytes;
    }

    return 0;
}

void *handle_client(void *arg)
{
    int index = *(int *)arg;
    free(arg);

    int client_fd = clients[index].socket;
    char buffer[BUFFER_SIZE];

    /* REGISTER */
    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received = recv(client_fd,
                                  buffer,
                                  sizeof(buffer) - 1,
                                  0);

    if (bytes_received <= 0)
    {
        close(client_fd);
        return NULL;
    }

    buffer[bytes_received] = '\0';

    char username[USERNAME_SIZE];

    if (sscanf(buffer,
               "REGISTER %49s",
               username) == 1)
    {
        int duplicate = 0;

        pthread_mutex_lock(&clients_mutex);

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].registered &&
                strcmp(clients[i].username,
                       username) == 0)
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
            clients[index].room_index = -1;
        }

        pthread_mutex_unlock(&clients_mutex);

        if (duplicate)
            send_response(client_fd,
                          "ERR 001 USERNAME_TAKEN");
        else
        {
            send_response(client_fd,
                          "OK REGISTERED");

            printf("User registered: %s\n",
                   username);
        }
    }
    else
    {
        send_response(client_fd,
                      "ERR 006 INVALID_COMMAND");
    }

    /* COMMAND LOOP */
    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        bytes_received = recv(client_fd,
                              buffer,
                              sizeof(buffer) - 1,
                              0);

        if (bytes_received <= 0)
            break;

        buffer[bytes_received] = '\0';

        printf("[%s] %s",
               clients[index].registered
                   ? clients[index].username
                   : "UNKNOWN",
               buffer);

        /* LIST */
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

            send(client_fd,
                 response,
                 strlen(response),
                 0);
        }

        /* BCAST */
        else if (strncmp(buffer, "BCAST ", 6) == 0)
        {
            char message[BUFFER_SIZE];

            strcpy(message, buffer + 6);
            message[strcspn(message, "\n")] = '\0';

            pthread_mutex_lock(&clients_mutex);

            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i].registered &&
                    i != index)
                {
                    char msg[BUFFER_SIZE];

                    snprintf(msg,
                             sizeof(msg),
                             "MSG BCAST %s %s\n",
                             clients[index].username,
                             message);

                    send(clients[i].socket,
                         msg,
                         strlen(msg),
                         0);
                }
            }

            pthread_mutex_unlock(&clients_mutex);

            send_response(client_fd,
                          "OK SENT");
        }

        /* PMSG */
        else if (strncmp(buffer, "PMSG ", 5) == 0)
        {
            char target[USERNAME_SIZE];
            char message[BUFFER_SIZE];

            if (sscanf(buffer + 5,
                       "%49s %[^\n]",
                       target,
                       message) < 2)
            {
                send_response(client_fd,
                              "ERR 006 INVALID_COMMAND");
                continue;
            }

            int target_index = -1;

            pthread_mutex_lock(&clients_mutex);

            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i].registered &&
                    strcmp(clients[i].username,
                           target) == 0)
                {
                    target_index = i;
                    break;
                }
            }

            if (target_index != -1)
            {
                char msg[BUFFER_SIZE];

                snprintf(msg,
                         sizeof(msg),
                         "MSG PMSG %s %s\n",
                         clients[index].username,
                         message);

                send(clients[target_index].socket,
                     msg,
                     strlen(msg),
                     0);
            }

            pthread_mutex_unlock(&clients_mutex);

            if (target_index == -1)
                send_response(client_fd,
                              "ERR 002 USER_NOT_FOUND");
            else
                send_response(client_fd,
                              "OK SENT");
        }

        /* CREATE */
        else if (strncmp(buffer, "CREATE ", 7) == 0)
        {
            char room_name[ROOM_NAME_SIZE];

            if (sscanf(buffer + 7,
                       "%49s",
                       room_name) != 1)
            {
                send_response(client_fd,
                              "ERR 006 INVALID_COMMAND");
                continue;
            }

            pthread_mutex_lock(&clients_mutex);

            if (find_room(room_name) != -1)
            {
                pthread_mutex_unlock(&clients_mutex);

                send_response(client_fd,
                              "ERR 003 ROOM_EXISTS");
                continue;
            }

            int room_index = find_free_room();

            if (room_index == -1)
            {
                pthread_mutex_unlock(&clients_mutex);

                send_response(client_fd,
                              "ERR 004 ROOM_LIMIT");
                continue;
            }

            strncpy(rooms[room_index].name,
                    room_name,
                    ROOM_NAME_SIZE - 1);

            rooms[room_index].name[ROOM_NAME_SIZE - 1] = '\0';
            rooms[room_index].active = 1;

            pthread_mutex_unlock(&clients_mutex);

            send_response(client_fd,
                          "OK ROOM_CREATED");
        }

        /* JOIN */
        else if (strncmp(buffer, "JOIN ", 5) == 0)
        {
            char room_name[ROOM_NAME_SIZE];

            if (sscanf(buffer + 5,
                       "%49s",
                       room_name) != 1)
            {
                send_response(client_fd,
                              "ERR 006 INVALID_COMMAND");
                continue;
            }

            pthread_mutex_lock(&clients_mutex);

            int room_index = find_room(room_name);

            if (room_index == -1)
            {
                pthread_mutex_unlock(&clients_mutex);

                send_response(client_fd,
                              "ERR 005 ROOM_NOT_FOUND");
                continue;
            }

            clients[index].room_index = room_index;

            pthread_mutex_unlock(&clients_mutex);

            send_response(client_fd,
                          "OK JOINED");
        }

        /* ROOMS */
        else if (strncmp(buffer, "ROOMS", 5) == 0)
        {
            char room_list[BUFFER_SIZE];
            room_list[0] = '\0';

            pthread_mutex_lock(&clients_mutex);

            int first_room = 1;

            for (int i = 0; i < MAX_ROOMS; i++)
            {
                if (rooms[i].active)
                {
                    if (!first_room)
                    {
                        strncat(room_list,
                                ",",
                                sizeof(room_list) -
                                strlen(room_list) - 1);
                    }

                    strncat(room_list,
                            rooms[i].name,
                            sizeof(room_list) -
                            strlen(room_list) - 1);

                    first_room = 0;
                }
            }

            pthread_mutex_unlock(&clients_mutex);

            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "OK ROOMS %s NID:%s\n",
                     room_list,
                     NID);

            send(client_fd,
                 response,
                 strlen(response),
                 0);
        }

        /* LEAVE */
        else if (strncmp(buffer, "LEAVE", 5) == 0)
        {
            pthread_mutex_lock(&clients_mutex);

            if (clients[index].room_index == -1)
            {
                pthread_mutex_unlock(&clients_mutex);

                send_response(client_fd,
                              "ERR 005 NOT_IN_ROOM");
                continue;
            }

            clients[index].room_index = -1;

            pthread_mutex_unlock(&clients_mutex);

            send_response(client_fd,
                          "OK LEFT");
        }

        /* RMSG */
        else if (strncmp(buffer, "RMSG ", 5) == 0)
        {
            char room_name[ROOM_NAME_SIZE];
            char message[BUFFER_SIZE];

            if (sscanf(buffer + 5,
                       "%49s %[^\n]",
                       room_name,
                       message) < 2)
            {
                send_response(client_fd,
                              "ERR 006 INVALID_COMMAND");
                continue;
            }

            pthread_mutex_lock(&clients_mutex);

            int room_index = find_room(room_name);

            if (room_index == -1)
            {
                pthread_mutex_unlock(&clients_mutex);

                send_response(client_fd,
                              "ERR 005 ROOM_NOT_FOUND");
                continue;
            }

            if (clients[index].room_index != room_index)
            {
                pthread_mutex_unlock(&clients_mutex);

                send_response(client_fd,
                              "ERR 005 NOT_IN_ROOM");
                continue;
            }

            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i].registered &&
                    clients[i].room_index == room_index &&
                    i != index)
                {
                    char msg[BUFFER_SIZE];

                    snprintf(msg,
                             sizeof(msg),
                             "MSG ROOM %s %s: %s\n",
                             room_name,
                             clients[index].username,
                             message);

                    send(clients[i].socket,
                         msg,
                         strlen(msg),
                         0);
                }
            }

            pthread_mutex_unlock(&clients_mutex);

            send_response(client_fd,
                          "OK SENT");
        }

        /* SENDFILE */
        else if (strncmp(buffer, "SENDFILE ", 9) == 0)
        {
            char target[USERNAME_SIZE];
            char filename[256];
            long long filesize;

            if (sscanf(buffer + 9,
                       "%49s %255s %lld",
                       target,
                       filename,
                       &filesize) != 3 ||
                filesize < 0)
            {
                send_response(client_fd,
                              "ERR 006 INVALID_COMMAND");
                continue;
            }

            int target_index = -1;

            pthread_mutex_lock(&clients_mutex);

            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i].registered &&
                    strcmp(clients[i].username,
                           target) == 0)
                {
                    target_index = i;
                    break;
                }
            }

            pthread_mutex_unlock(&clients_mutex);

            if (target_index == -1)
            {
                send_response(client_fd,
                              "ERR 002 USER_NOT_FOUND");
                continue;
            }

            char safe_filename[256];

            strncpy(safe_filename,
                    filename,
                    sizeof(safe_filename) - 1);

            safe_filename[sizeof(safe_filename) - 1] = '\0';

            char *slash = strrchr(safe_filename, '/');

            if (slash != NULL)
                memmove(safe_filename,
                        slash + 1,
                        strlen(slash + 1) + 1);

            char filepath[512];

            snprintf(filepath,
                     sizeof(filepath),
                     "%s/%s",
                     STORAGE_PATH,
                     safe_filename);

            FILE *file = fopen(filepath, "wb");

            if (file == NULL)
            {
                perror("fopen");

                send_response(client_fd,
                              "ERR 007 FILE_ERROR");
                continue;
            }

            printf("Receiving file: %s (%lld bytes)\n",
                   safe_filename,
                   filesize);

            if (receive_exact_bytes(client_fd,
                                     file,
                                     filesize) != 0)
            {
                fclose(file);

                send_response(client_fd,
                              "ERR 007 FILE_ERROR");
                continue;
            }

            fclose(file);

            printf("File received successfully: %s\n",
                   filepath);

            pthread_mutex_lock(&clients_mutex);

            char notification[BUFFER_SIZE];

            snprintf(notification,
                     sizeof(notification),
                     "FILE %s %s %lld\n",
                     clients[index].username,
                     safe_filename,
                     filesize);

            send(target_index == index
                     ? client_fd
                     : clients[target_index].socket,
                 notification,
                 strlen(notification),
                 0);

            pthread_mutex_unlock(&clients_mutex);

            send_response(client_fd,
                          "OK FILE_SENT");
        }

        /* QUIT */
        else if (strncmp(buffer, "QUIT", 4) == 0)
        {
            send_response(client_fd,
                          "OK BYE");
            break;
        }

        /* INVALID */
        else
        {
            send_response(client_fd,
                          "ERR 006 INVALID_COMMAND");
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
    clients[index].room_index = -1;

    pthread_mutex_unlock(&clients_mutex);

    return NULL;
}

int main(void)
{
    int server_fd;
    struct sockaddr_in server_addr;

    mkdir("./storage", 0755);
    mkdir(STORAGE_PATH, 0755);

    memset(clients, 0, sizeof(clients));
    memset(rooms, 0, sizeof(rooms));

    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i].room_index = -1;

    server_fd = socket(AF_INET,
                       SOCK_STREAM,
                       0);

    if (server_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;

    setsockopt(server_fd,
               SOL_SOCKET,
               SO_REUSEADDR,
               &opt,
               sizeof(opt));

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

    if (listen(server_fd,
               BACKLOG) < 0)
    {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("NetMessenger server started.\n");
    printf("Listening on TCP port %d\n", PORT);
    printf("Storage: %s\n", STORAGE_PATH);
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

            send_response(client_fd,
                          "ERR 008 SERVER_FULL");

            close(client_fd);
            continue;
        }

        clients[index].socket = client_fd;
        clients[index].registered = 0;
        clients[index].username[0] = '\0';
        clients[index].room_index = -1;

        pthread_mutex_unlock(&clients_mutex);

        printf("Client connected from %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        int *thread_index = malloc(sizeof(int));

        if (thread_index == NULL)
        {
            perror("malloc");
            close(client_fd);
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
