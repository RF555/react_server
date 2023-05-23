#include "reactor.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @brief Pointer to the main reactor.
 */
void *main_reactor = NULL;

/**
 * @brief Count the clients number.
 */
int client_count = 0;

// The total number of bytes received from clients in the server's lifetime.
uint64_t total_bytes_received = 0;

// The total number of bytes sent to clients in the server's lifetime.
uint64_t total_bytes_sent = 0;

int main(void) {
    struct sockaddr_in server_addr;
    int server_fd = -1;
    int reuse = 1;

    signal(SIGINT, signal_handler);

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DEFAULT_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return 1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
        fprintf(stderr, "setsockopt(SO_REUSEADDR) failed: %s\n", strerror(errno));
        close(server_fd);
        return 1;
    }

    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "bind() failed: %s\n", strerror(errno));
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, MAX_CLIENT) < 0) {
        fprintf(stderr, "listen() failed: %s\n", strerror(errno));
        close(server_fd);
        return 1;
    }

    fprintf(stdout, "Server listening on port %d.\n", DEFAULT_PORT);

    main_reactor = createReactor();

    if (main_reactor == NULL) {
        fprintf(stderr, "createReactor() failed: %s\n", strerror(ENOSPC));
        close(server_fd);
        return 1;
    }

    addFd(main_reactor, server_fd, server_handler);

    startReactor(main_reactor);
    WaitFor(main_reactor);

    signal_handler();

    return 0;
}

void signal_handler() {
    fprintf(stdout, "Server shutting down...\n");

    if (main_reactor != NULL) {
        if (((reactor_struct_ptr) main_reactor)->is_running == YES)
            stopReactor(main_reactor);

        fprintf(stdout, "Closing all sockets and freeing memory...\n");

        fd_reactor_node_ptr curr_fd = ((reactor_struct_ptr) main_reactor)->src;
        fd_reactor_node_ptr prev_fd = NULL;

        while (curr_fd != NULL) {
            prev_fd = curr_fd;
            curr_fd = curr_fd->next_fd;

            close(prev_fd->fd);
            free(prev_fd);
        }

        free(main_reactor);

        fprintf(stdout, "Memory cleanup complete, may the force be with you.\n");
        fprintf(stdout, "Statistics:\n");
        fprintf(stdout, "Client count in this session: %d\n", client_count);
        fprintf(stdout, "Total bytes received in this session: %lu bytes (%lu KB).\n",
                total_bytes_received, total_bytes_received / 1024);
        fprintf(stdout, "Total bytes sent in this session: %lu bytes (%lu KB).\n", total_bytes_sent,
                total_bytes_sent / 1024);
    } else
        fprintf(stdout, "main_Reactor wasn't created, no memory cleanup needed.\n");

    exit(0);
}

void *client_handler(int fd, void *react_ptr) {
    char *buffer = (char *) calloc(MAX_BUFFER, sizeof(char));

    if (buffer == NULL) {
        fprintf(stderr, "calloc() failed: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    int bytes_read = recv(fd, buffer, MAX_BUFFER, 0);

    if (bytes_read <= 0) {
        if (bytes_read < 0)
            fprintf(stderr, "recv() failed: %s\n", strerror(errno));

        else
            fprintf(stdout, "Client %d disconnected.\n", fd);

        free(buffer);
        close(fd);
        return NULL;
    }

    total_bytes_received += bytes_read;

    // Make sure the buffer is null-terminated, so we can print it.
    if (bytes_read < MAX_BUFFER)
        *(buffer + bytes_read) = '\0';

    else
        *(buffer + MAX_BUFFER - 1) = '\0';

    // Remove the arrow keys from the buffer, as they are not printable and mess up the output,
    // and replace them with spaces, so the rest of the message won't cut off.
    for (int i = 0; i < bytes_read - 3; i++) {
        if ((*(buffer + i) == 0x1b) && (*(buffer + i + 1) == 0x5b) &&
            (*(buffer + i + 2) == 0x41 || *(buffer + i + 2) == 0x42 || *(buffer + i + 2) == 0x43 ||
             *(buffer + i + 2) == 0x44)) {
            *(buffer + i) = 0x20;
            *(buffer + i + 1) = 0x20;
            *(buffer + i + 2) = 0x20;

            i += 2;
        }
    }

    fprintf(stdout, "Client %d: %s\n", fd, buffer);

    // Send the message back to all except the sender.
    // We don't need to send it back to the sender, as the sender already has the message.
    // We also don't need to send it back to the server listening socket, as it will result in an error.
    // We also know that the server listening socket is the first node in the list, so we can skip it,
    // and start from the second node, which can't be NULL, as we already know there is at least one client connected.
    fd_reactor_node_ptr curr_fd = ((reactor_struct_ptr) react_ptr)->src->next_fd;

    while (curr_fd != NULL) {
        if (curr_fd->fd != fd) {
            int bytes_write = send(curr_fd->fd, buffer, bytes_read, 0);

            if (bytes_write < 0) {
                fprintf(stderr, "send() failed: %s\n", strerror(errno));
                free(buffer);
                return NULL;
            } else if (bytes_write == 0)
                fprintf(stderr, "Client %d disconnected, expecting to be remove in next_fd poll() round.\n",
                        curr_fd->fd);

            else if (bytes_write < bytes_read)
                fprintf(stderr, "send() sent less bytes than expected, check your network.\n");

            else
                total_bytes_sent += bytes_write;
        }

        curr_fd = curr_fd->next_fd;
    }

    free(buffer);

    return react_ptr;
}

void *server_handler(int fd, void *react_ptr) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    reactor_struct_ptr reactor = (reactor_struct_ptr) react_ptr;

    // Sanity check.
    if (reactor == NULL) {
        fprintf(stderr, "Server handler error: %s\n", strerror(EINVAL));
        return NULL;
    }

    int client_fd = accept(fd, (struct sockaddr *) &client_addr, &client_len);

    // Sanity check.
    if (client_fd < 0) {
        fprintf(stderr, "accept() failed: %s\n", strerror(errno));
        return NULL;
    }

    // Add the client to the reactor.
    addFd(reactor, client_fd, client_handler);

    client_count++;

    fprintf(stdout, "Client %s:%d connected, ID: %d\n", inet_ntoa(client_addr.sin_addr),
            ntohs(client_addr.sin_port), client_fd);

    return react_ptr;
}