/*
 *  Operation Systems (OSs) Course Assignment 4
 *  Reactor - A TCP server that handles multiple clients using a reactor.
 *  Copyright (C) 2023  Roy Simanovich and Linor Ronen
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "reactor.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// The reactor pointer.
void *reactor = NULL;

// The number of clients connected to the server in its lifetime.
uint32_t client_count = 0;

// The total number of bytes received from clients in the server's lifetime.
uint64_t total_bytes_received = 0;

// The total number of bytes sent to clients in the server's lifetime.
uint64_t total_bytes_sent = 0;

int main(void) {
    struct sockaddr_in server_addr;
    int server_fd = -1, reuse = 1;

    fprintf(stdout, "%s", C_INFO_LICENSE);

    signal(SIGINT, signal_handler);

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "%s socket() failed: %s\n", C_PREFIX_ERROR, strerror(errno));
        return EXIT_FAILURE;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
        fprintf(stderr, "%s setsockopt(SO_REUSEADDR) failed: %s\n", C_PREFIX_ERROR, strerror(errno));
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "%s bind() failed: %s\n", C_PREFIX_ERROR, strerror(errno));
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, MAX_QUEUE) < 0) {
        fprintf(stderr, "%s listen() failed: %s\n", C_PREFIX_ERROR, strerror(errno));
        close(server_fd);
        return EXIT_FAILURE;
    }

    fprintf(stdout, "%s Server listening on port \033[0;32m%d\033[0;37m.\n", C_PREFIX_INFO, SERVER_PORT);

    reactor = createReactor();

    if (reactor == NULL) {
        fprintf(stderr, "%s createReactor() failed: %s\n", C_PREFIX_ERROR, strerror(ENOSPC));
        close(server_fd);
        return EXIT_FAILURE;
    }

    addFd(reactor, server_fd, server_handler);

    startReactor(reactor);
    WaitFor(reactor);

    signal_handler();

    return EXIT_SUCCESS;
}

void signal_handler() {
    fprintf(stdout, "%s%s Server shutting down...\n", MACRO_CLEANUP, C_PREFIX_INFO);

    if (reactor != NULL) {
        if (((reactor_t_ptr) reactor)->running)
            stopReactor(reactor);

        fprintf(stdout, "%s Closing all sockets and freeing memory...\n", C_PREFIX_INFO);

        reactor_node_ptr curr = ((reactor_t_ptr) reactor)->head;
        reactor_node_ptr prev = NULL;

        while (curr != NULL) {
            prev = curr;
            curr = curr->next;

            close(prev->fd);
            free(prev);
        }

        free(reactor);

        fprintf(stdout, "%s Memory cleanup complete, may the force be with you.\n", C_PREFIX_INFO);
        fprintf(stdout, "%s Statistics:\n", C_PREFIX_INFO);
        fprintf(stdout, "%s Client count in this session: %d\n", C_PREFIX_INFO, client_count);
        fprintf(stdout, "%s Total bytes received in this session: %lu bytes (%lu KB).\n", C_PREFIX_INFO,
                total_bytes_received, total_bytes_received / 1024);
        fprintf(stdout, "%s Total bytes sent in this session: %lu bytes (%lu KB).\n", C_PREFIX_INFO, total_bytes_sent,
                total_bytes_sent / 1024);
    } else
        fprintf(stdout, "%s Reactor wasn't created, no memory cleanup needed.\n", C_PREFIX_INFO);

    exit(EXIT_SUCCESS);
}

void *client_handler(int fd, void *react) {
    char *buf = (char *) calloc(MAX_BUFFER, sizeof(char));

    if (buf == NULL) {
        fprintf(stderr, "%s calloc() failed: %s\n", C_PREFIX_ERROR, strerror(errno));
        close(fd);
        return NULL;
    }

    int bytes_read = recv(fd, buf, MAX_BUFFER, 0);

    if (bytes_read <= 0) {
        if (bytes_read < 0)
            fprintf(stderr, "%s recv() failed: %s\n", C_PREFIX_ERROR, strerror(errno));

        else
            fprintf(stdout, "%s Client %d disconnected.\n", C_PREFIX_WARNING, fd);

        free(buf);
        close(fd);
        return NULL;
    }

    total_bytes_received += bytes_read;

    // Make sure the buffer is null-terminated, so we can print it.
    if (bytes_read < MAX_BUFFER)
        *(buf + bytes_read) = '\0';

    else
        *(buf + MAX_BUFFER - 1) = '\0';

    // Remove the arrow keys from the buffer, as they are not printable and mess up the output,
    // and replace them with spaces, so the rest of the message won't cut off.
    for (int i = 0; i < bytes_read - 3; i++) {
        if ((*(buf + i) == 0x1b) && (*(buf + i + 1) == 0x5b) &&
            (*(buf + i + 2) == 0x41 || *(buf + i + 2) == 0x42 || *(buf + i + 2) == 0x43 || *(buf + i + 2) == 0x44)) {
            *(buf + i) = 0x20;
            *(buf + i + 1) = 0x20;
            *(buf + i + 2) = 0x20;

            i += 2;
        }
    }

    fprintf(stdout, "%s Client %d: %s\n", C_PREFIX_MESSAGE, fd, buf);

    // Send the message back to all except the sender.
    // We don't need to send it back to the sender, as the sender already has the message.
    // We also don't need to send it back to the server listening socket, as it will result in an error.
    // We also know that the server listening socket is the first node in the list, so we can skip it,
    // and start from the second node, which can't be NULL, as we already know there is at least one client connected.
    reactor_node_ptr curr = ((reactor_t_ptr) react)->head->next;

    while (curr != NULL) {
        if (curr->fd != fd) {
            int bytes_write = send(curr->fd, buf, bytes_read, 0);

            if (bytes_write < 0) {
                fprintf(stderr, "%s send() failed: %s\n", C_PREFIX_ERROR, strerror(errno));
                free(buf);
                return NULL;
            } else if (bytes_write == 0)
                fprintf(stderr, "%s Client %d disconnected, expecting to be remove in next poll() round.\n",
                        C_PREFIX_WARNING, curr->fd);

            else if (bytes_write < bytes_read)
                fprintf(stderr, "%s send() sent less bytes than expected, check your network.\n", C_PREFIX_WARNING);

            else
                total_bytes_sent += bytes_write;
        }

        curr = curr->next;
    }

    free(buf);

    return react;
}

void *server_handler(int fd, void *react) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    reactor_t_ptr reactor = (reactor_t_ptr) react;

    // Sanity check.
    if (reactor == NULL) {
        fprintf(stderr, "%s Server handler error: %s\n", C_PREFIX_ERROR, strerror(EINVAL));
        return NULL;
    }

    int client_fd = accept(fd, (struct sockaddr *) &client_addr, &client_len);

    // Sanity check.
    if (client_fd < 0) {
        fprintf(stderr, "%s accept() failed: %s\n", C_PREFIX_ERROR, strerror(errno));
        return NULL;
    }

    // Add the client to the reactor.
    addFd(reactor, client_fd, client_handler);

    client_count++;

    fprintf(stdout, "%s Client %s:%d connected, ID: %d\n", C_PREFIX_INFO, inet_ntoa(client_addr.sin_addr),
            ntohs(client_addr.sin_port), client_fd);

    return react;
}