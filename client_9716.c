/*#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 15716
#define SERVER_IP "127.0.0.1"

int main(void)
{
    int sock_fd;
    struct sockaddr_in server_addr;

  
        perror("socket");
      exit(EXIT_FAILURE);
    }

  _addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to NetMessenger server.\n");
    printf("Server: %s:%d\n", SERVER_IP, PORT);

    close(sock_fd);

    return 0;
}
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 15716
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024

int main(void)
{
    int sock_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    /* Create TCP socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Configure server address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    /* Connect to server */
    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to NetMessenger server.\n");
    printf("Server: %s:%d\n", SERVER_IP, PORT);

    /* Register */
    printf("Enter username: ");

    char username[50];

    if (scanf("%49s", username) != 1)
    {
        close(sock_fd);
        return 1;
    }

    snprintf(buffer, sizeof(buffer),
             "REGISTER %s\n", username);

    send(sock_fd, buffer, strlen(buffer), 0);

    /* Receive server response */
    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received = recv(sock_fd,
                                  buffer,
                                  sizeof(buffer) - 1,
                                  0);

    if (bytes_received > 0)
    {
        buffer[bytes_received] = '\0';
        printf("Server: %s", buffer);
    }

    close(sock_fd);

    return 0;
}
