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
#include <time.h>
#include <stdarg.h>

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
#define LOG_FILE "netmsg_IT23779716.log"

typedef struct {
    int socket;
    char username[USERNAME_SIZE];
    int registered;
    int room_index;
} Client;

typedef struct {
    char name[ROOM_NAME_SIZE];
    int active;
} Room;

Client clients[MAX_CLIENTS];
Room rooms[MAX_ROOMS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---------- Logging ---------- */

void log_event(const char *format, ...)
{
    FILE *fp;
    time_t now;
    struct tm timeinfo;
    char timestamp[64];
    va_list args;

    fp = fopen(LOG_FILE, "a");
    if (fp == NULL)
        return;

    now = time(NULL);
    localtime_r(&now, &timeinfo);

    strftime(timestamp, sizeof(timestamp),
             "%Y-%m-%d %H:%M:%S", &timeinfo);

    fprintf(fp, "[%s] ", timestamp);

    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);
}

/* ---------- Utility Functions ---------- */

int find_free_slot()
{
    int i;

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].registered)
            return i;
    }

    return -1;
}

int find_client_by_username(const char *username)
{
    int i;

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].registered &&
            strcmp(clients[i].username, username) == 0) {
            return i;
        }
    }

    return -1;
}

int find_room(const char *room_name)
{
    int i;

    for (i = 0; i < MAX_ROOMS; i++) {
        if (rooms[i].active &&
            strcmp(rooms[i].name, room_name) == 0) {
            return i;
        }
    }

    return -1;
}

int find_free_room()
{
    int i;

    for (i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].active)
            return i;
    }

    return -1;
}

void send_response(int socket, const char *response)
{
    char message[BUFFER_SIZE];

    snprintf(message, sizeof(message),
             "%s NID:%s\n", response, NID);

    send(socket, message, strlen(message), 0);
}

/* ---------- Exact File Reception ---------- */

int receive_exact_bytes(int socket, FILE *fp, long filesize)
{
    char buffer[FILE_BUFFER_SIZE];
    long remaining = filesize;

    while (remaining > 0) {

        int chunk = (remaining > FILE_BUFFER_SIZE)
                        ? FILE_BUFFER_SIZE
                        : (int)remaining;

        int received = recv(socket, buffer, chunk, 0);

        if (received <= 0)
            return -1;

        fwrite(buffer, 1, received, fp);
        remaining -= received;
    }

    return 0;
}

/* ---------- Client Handler ---------- */

void *handle_client(void *arg)
{
    Client *client = (Client *)arg;
    int socket = client->socket;

    char buffer[BUFFER_SIZE];

    /* Registration */
    memset(buffer, 0, sizeof(buffer));

    int bytes = recv(socket, buffer, sizeof(buffer) - 1, 0);

    if (bytes <= 0) {
        log_event("Client disconnected before registration");
        close(socket);
        client->registered = 0;
        return NULL;
    }

    buffer[bytes] = '\0';

    if (strncmp(buffer, "REGISTER ", 9) != 0) {
        send_response(socket, "ERR 006 INVALID_COMMAND");
        log_event("Registration failed: invalid command");
        close(socket);
        client->registered = 0;
        return NULL;
    }

    char username[USERNAME_SIZE];

    sscanf(buffer + 9, "%49s", username);

    pthread_mutex_lock(&clients_mutex);

    if (find_client_by_username(username) != -1) {

        pthread_mutex_unlock(&clients_mutex);

        send_response(socket, "ERR 001 USERNAME_TAKEN");
        log_event("Registration rejected: username '%s' already exists",
                  username);

        close(socket);
        client->registered = 0;

        return NULL;
    }

    strncpy(client->username, username, USERNAME_SIZE - 1);
    client->username[USERNAME_SIZE - 1] = '\0';

    client->registered = 1;
    client->room_index = -1;

    pthread_mutex_unlock(&clients_mutex);

    send_response(socket, "OK REGISTERED");

    log_event("User registered: %s", client->username);

    /* ---------- Command Loop ---------- */

    while (1) {

        memset(buffer, 0, sizeof(buffer));

        bytes = recv(socket, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            log_event("Client disconnected: %s",
                      client->username);
            break;
        }

        buffer[bytes] = '\0';

        /* Remove newline */
        buffer[strcspn(buffer, "\r\n")] = '\0';

        log_event("Command from %s: %s",
                  client->username, buffer);

        /* ---------- LIST ---------- */

        if (strcmp(buffer, "LIST") == 0) {

            char response[BUFFER_SIZE] = "USERS";

            pthread_mutex_lock(&clients_mutex);

            for (int i = 0; i < MAX_CLIENTS; i++) {

                if (clients[i].registered) {

                    strcat(response, " ");
                    strcat(response, clients[i].username);
                }
            }

            pthread_mutex_unlock(&clients_mutex);

            send_response(socket, response);

            log_event("LIST requested by %s",
                      client->username);
        }

        /* ---------- BCAST ---------- */

        else if (strncmp(buffer, "BCAST ", 6) == 0) {

            char message[BUFFER_SIZE];

            snprintf(message, sizeof(message),
                     "MSG %s %s\n",
                     client->username,
                     buffer + 6);

            pthread_mutex_lock(&clients_mutex);

            for (int i = 0; i < MAX_CLIENTS; i++) {

                if (clients[i].registered &&
                    clients[i].socket != socket) {

                    send(clients[i].socket,
                         message,
                         strlen(message),
                         0);
                }
            }

            pthread_mutex_unlock(&clients_mutex);

            send_response(socket, "OK BCAST");

            log_event("Broadcast from %s: %s",
                      client->username,
                      buffer + 6);
        }

        /* ---------- PMSG ---------- */

        else if (strncmp(buffer, "PMSG ", 5) == 0) {

            char target[USERNAME_SIZE];
            char message[BUFFER_SIZE];

            if (sscanf(buffer + 5, "%49s %[^\n]",
                       target, message) < 2) {

                send_response(socket, "ERR 006 INVALID_COMMAND");
                continue;
            }

            pthread_mutex_lock(&clients_mutex);

            int target_index =
                find_client_by_username(target);

            if (target_index == -1) {

                pthread_mutex_unlock(&clients_mutex);

                send_response(socket, "ERR 002 USER_NOT_FOUND");

                log_event("PMSG failed: %s -> %s, user not found",
                          client->username,
                          target);

                continue;
            }

            char msg[BUFFER_SIZE];

            snprintf(msg, sizeof(msg),
                     "MSG %s %s\n",
                     client->username,
                     message);

            send(clients[target_index].socket,
                 msg,
                 strlen(msg),
                 0);

            pthread_mutex_unlock(&clients_mutex);

            send_response(socket, "OK PMSG");

            log_event("Private message: %s -> %s",
                      client->username,
                      target);
        }

        /* ---------- CREATE ROOM ---------- */

        else if (strncmp(buffer, "CREATE ", 7) == 0) {

            char room_name[ROOM_NAME_SIZE];

            sscanf(buffer + 7, "%49s", room_name);

            if (find_room(room_name) != -1) {

                send_response(socket,
                              "ERR 004 ROOM_EXISTS");

                continue;
            }

            int room_index = find_free_room();

            if (room_index == -1) {

                send_response(socket,
                              "ERR 007 ROOM_LIMIT");

                continue;
            }

            strcpy(rooms[room_index].name,
                   room_name);

            rooms[room_index].active = 1;

            send_response(socket, "OK ROOM_CREATED");

            log_event("Room created: %s by %s",
                      room_name,
                      client->username);
        }

        /* ---------- JOIN ROOM ---------- */

        else if (strncmp(buffer, "JOIN ", 5) == 0) {

            char room_name[ROOM_NAME_SIZE];

            sscanf(buffer + 5, "%49s", room_name);

            int room_index = find_room(room_name);

            if (room_index == -1) {

                send_response(socket,
                              "ERR 005 ROOM_NOT_FOUND");

                log_event("JOIN failed: %s -> %s",
                          client->username,
                          room_name);

                continue;
            }

            client->room_index = room_index;

            send_response(socket, "OK JOINED");

            log_event("User %s joined room %s",
                      client->username,
                      room_name);
        }

        /* ---------- ROOMS ---------- */

        else if (strcmp(buffer, "ROOMS") == 0) {

            char response[BUFFER_SIZE] = "ROOMS";

            for (int i = 0; i < MAX_ROOMS; i++) {

                if (rooms[i].active) {

                    strcat(response, " ");
                    strcat(response,
                           rooms[i].name);
                }
            }

            send_response(socket, response);

            log_event("ROOMS requested by %s",
                      client->username);
        }

        /* ---------- LEAVE ---------- */

        else if (strcmp(buffer, "LEAVE") == 0) {

            client->room_index = -1;

            send_response(socket, "OK LEFT");

            log_event("User %s left room",
                      client->username);
        }

        /* ---------- RMSG ---------- */

        else if (strncmp(buffer, "RMSG ", 5) == 0) {

            char room_name[ROOM_NAME_SIZE];
            char message[BUFFER_SIZE];

            if (sscanf(buffer + 5, "%49s %[^\n]",
                       room_name, message) < 2) {

                send_response(socket,
                              "ERR 006 INVALID_COMMAND");

                continue;
            }

            int room_index = find_room(room_name);

            if (room_index == -1) {

                send_response(socket,
                              "ERR 005 ROOM_NOT_FOUND");

                continue;
            }

            if (client->room_index != room_index) {

                send_response(socket,
                              "ERR 003 NOT_IN_ROOM");

                continue;
            }

            char msg[BUFFER_SIZE];

            snprintf(msg, sizeof(msg),
                     "MSG %s %s\n",
                     client->username,
                     message);

            pthread_mutex_lock(&clients_mutex);

            for (int i = 0; i < MAX_CLIENTS; i++) {

                if (clients[i].registered &&
                    clients[i].room_index == room_index &&
                    clients[i].socket != socket) {

                    send(clients[i].socket,
                         msg,
                         strlen(msg),
                         0);
                }
            }

            pthread_mutex_unlock(&clients_mutex);

            send_response(socket, "OK RMSG");

            log_event("Room message in %s from %s",
                      room_name,
                      client->username);
        }

        /* ---------- SENDFILE ---------- */

        else if (strncmp(buffer, "SENDFILE ", 9) == 0) {

            char target[USERNAME_SIZE];
            char filename[256];
            long filesize;

            if (sscanf(buffer + 9,
                       "%49s %255s %ld",
                       target,
                       filename,
                       &filesize) != 3) {

                send_response(socket,
                              "ERR 006 INVALID_COMMAND");

                continue;
            }

            pthread_mutex_lock(&clients_mutex);

            int target_index =
                find_client_by_username(target);

            pthread_mutex_unlock(&clients_mutex);

            if (target_index == -1) {

                send_response(socket,
                              "ERR 002 USER_NOT_FOUND");

                log_event("File transfer failed: target %s not found",
                          target);

                continue;
            }

            /* Remove any path components */
            char *base = strrchr(filename, '/');

            if (base != NULL)
                memmove(filename,
                        base + 1,
                        strlen(base));

            char filepath[512];

            snprintf(filepath,
                     sizeof(filepath),
                     "%s/%s",
                     STORAGE_PATH,
                     filename);

            FILE *fp = fopen(filepath, "wb");

            if (fp == NULL) {

                send_response(socket,
                              "ERR 008 FILE_ERROR");

                log_event("File error: cannot create %s",
                          filepath);

                continue;
            }

            log_event("File transfer started: %s -> %s, file=%s, size=%ld",
                      client->username,
                      target,
                      filename,
                      filesize);

            int result =
                receive_exact_bytes(socket,
                                    fp,
                                    filesize);

            fclose(fp);

            if (result != 0) {

                send_response(socket,
                              "ERR 008 FILE_ERROR");

                log_event("File transfer failed: %s",
                          filename);

                continue;
            }

            char file_msg[BUFFER_SIZE];

            snprintf(file_msg,
                     sizeof(file_msg),
                     "FILE %s %s %ld\n",
                     client->username,
                     filename,
                     filesize);

            pthread_mutex_lock(&clients_mutex);

            send(clients[target_index].socket,
                 file_msg,
                 strlen(file_msg),
                 0);

            pthread_mutex_unlock(&clients_mutex);

            send_response(socket,
                          "OK FILE_SENT");

            log_event("File transfer completed: %s -> %s, file=%s, size=%ld",
                      client->username,
                      target,
                      filename,
                      filesize);
        }

        /* ---------- QUIT ---------- */

        else if (strcmp(buffer, "QUIT") == 0) {

            send_response(socket, "OK BYE");

            log_event("User disconnected: %s",
                      client->username);

            break;
        }

        /* ---------- INVALID COMMAND ---------- */

        else {

            send_response(socket,
                          "ERR 006 INVALID_COMMAND");

            log_event("Invalid command from %s: %s",
                      client->username,
                      buffer);
        }
    }

    close(socket);

    pthread_mutex_lock(&clients_mutex);

    client->registered = 0;
    client->username[0] = '\0';
    client->room_index = -1;

    pthread_mutex_unlock(&clients_mutex);

    return NULL;
}

/* ---------- Main ---------- */

int main()
{
    int server_socket;
    struct sockaddr_in server_addr;

    mkdir("./storage", 0777);
    mkdir(STORAGE_PATH, 0777);

    /* Create/append log */
    log_event("========================================");
    log_event("NetMessenger server starting");
    log_event("Server port: %d", PORT);
    log_event("Node ID: NID:%s", NID);
    log_event("Storage path: %s", STORAGE_PATH);

    server_socket = socket(AF_INET,
                           SOCK_STREAM,
                           0);

    if (server_socket < 0) {

        perror("socket");

        log_event("ERROR: socket creation failed");

        return 1;
    }

    int opt = 1;

    setsockopt(server_socket,
               SOL_SOCKET,
               SO_REUSEADDR,
               &opt,
               sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {

        perror("bind");

        log_event("ERROR: bind failed on port %d",
                  PORT);

        close(server_socket);

        return 1;
    }

    if (listen(server_socket, BACKLOG) < 0) {

        perror("listen");

        log_event("ERROR: listen failed");

        close(server_socket);

        return 1;
    }

    printf("Server listening on port %d...\n",
           PORT);

    log_event("Server listening on port %d",
              PORT);

    while (1) {

        struct sockaddr_in client_addr;
        socklen_t client_len =
            sizeof(client_addr);

        int client_socket =
            accept(server_socket,
                   (struct sockaddr *)&client_addr,
                   &client_len);

        if (client_socket < 0) {

            perror("accept");

            log_event("ERROR: accept failed");

            continue;
        }

        pthread_mutex_lock(&clients_mutex);

        int slot = find_free_slot();

        if (slot == -1) {

            pthread_mutex_unlock(&clients_mutex);

            send_response(client_socket,
                          "ERR 009 SERVER_FULL");

            close(client_socket);

            log_event("Connection rejected: server full");

            continue;
        }

        clients[slot].socket = client_socket;
        clients[slot].registered = 0;
        clients[slot].username[0] = '\0';
        clients[slot].room_index = -1;

        char client_ip[INET_ADDRSTRLEN];

        inet_ntop(AF_INET,
                  &client_addr.sin_addr,
                  client_ip,
                  sizeof(client_ip));

        int client_port =
            ntohs(client_addr.sin_port);

        printf("Client connected: %s:%d\n",
               client_ip,
               client_port);

        log_event("Client connected: %s:%d",
                  client_ip,
                  client_port);

        pthread_mutex_unlock(&clients_mutex);

        pthread_t thread;

        if (pthread_create(&thread,
                           NULL,
                           handle_client,
                           &clients[slot]) != 0) {

            perror("pthread_create");

            log_event("ERROR: pthread creation failed");

            close(client_socket);

            pthread_mutex_lock(&clients_mutex);
            clients[slot].registered = 0;
            pthread_mutex_unlock(&clients_mutex);

            continue;
        }

        pthread_detach(thread);
    }

    close(server_socket);

    return 0;
}
