#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "st_reactor.h"

/**
 * @brief Pointer to the reactor.
 */
void *reactor = NULL;
/**
 * @brief Counts the clients number.
 */
int client_count = 0;

int main(void) {
    struct sockaddr_in server_address;
    int server_fd = -1;
    int reuse_flag = 1;

    signal(SIGINT, signal_handler);

    memset(&server_address, 0, sizeof(server_address));

    server_address.sin_family = AF_INET; // using IPv4
    server_address.sin_port = htons(DEFAULT_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_flag, sizeof(int)) < 0) {
        fprintf(stderr, "setsockopt(SO_REUSEADDR) failed: %s\n", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (bind(server_fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        fprintf(stderr, "bind() failed: %s\n", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, MAX_CLIENT) < 0) {
        fprintf(stderr, "listen() failed: %s\n", strerror(errno));
        close(server_fd);
        return -1;
    }

    fprintf(stdout, "Server listening on port %d.\n", DEFAULT_PORT);

    reactor = createReactor();

    if (reactor == NULL) {
        fprintf(stderr, "createReactor() failed: %s\n", strerror(ENOSPC));
        close(server_fd);
        return -1;
    }

    addFd(reactor, server_fd, server_handler);

    startReactor(reactor);
    WaitFor(reactor);
    signal_handler();
    return 0;
}

void signal_handler() {
    fprintf(stdout, "Server shutting down...\n(handling SIGINT)\n");

    if (reactor != NULL) { // need to free everything
        if (((reactor_struct_ptr) reactor)->is_running == YES) {
            stopReactor(reactor);
        }

        fprintf(stdout, "Closing all sockets and freeing memory...\n");

        fd_node_ptr curr_node = ((reactor_struct_ptr) reactor)->src;
        fd_node_ptr prev_node = NULL;

        while (curr_node != NULL) {
            prev_node = curr_node;
            curr_node = curr_node->next_fd;
            close(prev_node->fd);
            free(prev_node);
        }
        free(reactor);
    }

    exit(0);
}

void *client_handler(int fd, void *reactor_ptr) {
    char *buffer = (char *) calloc(MAX_INPUT, sizeof(char));
    if (buffer == NULL) {
        fprintf(stderr, "calloc() failed: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    int bytes_read = recv(fd, buffer, MAX_INPUT, 0);

    if (bytes_read <= 0) {
        if (bytes_read < 0) {
            fprintf(stderr, "recv() failed: %s\n", strerror(errno));
        } else {
            fprintf(stdout, "Client %d disconnected.\n", fd);
        }
        free(buffer);
        close(fd);
        return NULL;
    }

    if (bytes_read < MAX_INPUT) {
        *(buffer + bytes_read) = '\0';
    } else {
        *(buffer + MAX_INPUT - 1) = '\0';
    }

    fprintf(stdout, "Client %d: %s\n", fd, buffer);

    fd_node_ptr curr_node = ((reactor_struct_ptr) reactor_ptr)->src->next_fd;

    while (curr_node != NULL) {
        if (curr_node->fd != fd) {
            int bytes_write = send(curr_node->fd, buffer, bytes_read, 0);
            if (bytes_write < 0) {
                fprintf(stderr, "send() failed: %s\n", strerror(errno));
                free(buffer);
                return NULL;
            } else if (bytes_write == 0) {
                fprintf(stderr, "Client %d disconnected, expecting to be remove in next poll() round.\n",
                        curr_node->fd);
            } else if (bytes_write < bytes_read) {
                fprintf(stderr, "send() sent less bytes than expected, check your network.\n");
            }
        }
        curr_node = curr_node->next_fd;
    }

    free(buffer);
    return reactor_ptr;
}


void *server_handler(int fd, void *reactor_ptr) {
    struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);

    reactor_struct_ptr reactor = (reactor_struct_ptr) reactor_ptr;
    if (reactor == NULL) {
        fprintf(stderr, "%s\n", strerror(EINVAL));
        return NULL;
    }

    int client_fd = accept(fd, (struct sockaddr *) &client_address, &client_len);
    if (client_fd < 0) {
        fprintf(stderr, "accept() failed: %s\n", strerror(errno));
        return NULL;
    }

    addFd(reactor, client_fd, client_handler);

    ++client_count;
    fprintf(stdout, "Client %s:%d connected, ID: %d\n", inet_ntoa(client_address.sin_addr),
            ntohs(client_address.sin_port), client_fd);

    return reactor_ptr;
}


