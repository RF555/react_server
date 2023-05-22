/*
** poll_server.c -- a cheesy multi-person chat server
 * Beej's guide - page 46
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#define PORT "9034"   // Port we're listening on

// Get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) { // IPv4
        return &(((struct sockaddr_in *) sa)->sin_addr);
    } else { // IPv6
        return &(((struct sockaddr_in6 *) sa)->sin6_addr);
    }
}

// Start a listening socket and return it's fd
int get_listener_socket(void) {
    int listener;   // Listening socket descriptor
    int yes = 1;    // For setsockopt() SO_REUSEADDR, below
    int rv;

    struct addrinfo hints, *addr_info, *p;

    // Get a socket and bind it
    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_UNSPEC; // don't care IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    hints.ai_flags = AI_PASSIVE; // use my IP

    // load up address structs with getaddrinfo()
    if ((rv = getaddrinfo(NULL, PORT, &hints, &addr_info)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    // loop through all the addresses and bind to the first we can
    for (p = addr_info; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;
        }

        // allow the program to reuse the port if it is in use
        // Lose the pesky "address already in use" error message
        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }


        // bind socket and port
        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }
        break;
    }

    // If we got here, it means we didn't get bound
    if (p == NULL) {
        return -1;
    }

    freeaddrinfo(addr_info); // All done with this

    // Listen
    if (listen(listener, 10) == -1) {
        return -1;
    }

    return listener;
}

// Add a new file descriptor to the set
void add_to_poll_fds(struct pollfd *poll_fds[], int new_fd, int *fd_count, int *fd_size) {
    // If we don't have room, add more space in the poll_fds array
    if (*fd_count == *fd_size) {
        *fd_size *= 2; // Double the size
        *poll_fds = realloc(*poll_fds, sizeof(**poll_fds) * (*fd_size)); // reallocate array with twice the size
    }

    (*poll_fds)[*fd_count].fd = new_fd;
    (*poll_fds)[*fd_count].events = POLLIN; // Check ready-to-read

    (*fd_count)++;
}

// Remove an index from the set
void del_from_pfds(struct pollfd pfds[], int i, int *fd_count) {
    // Copy the one from the end over this one
    pfds[i] = pfds[*fd_count - 1];

    (*fd_count)--;
}

// Main
int main(void) {
    int listener; // Listening socket descriptor
    int new_fd;   // Newly accept()ed socket descriptor
    struct sockaddr_storage remote_addr; // Client address
    socklen_t addrlen;

    char buf[256]; // Buffer for client data

    char remoteIP[INET6_ADDRSTRLEN];

    // Start off with room for 5 connections
    // (We'll realloc as necessary)
    int fd_count = 0; // number of sockets
    int fd_size = 5; // array size
    struct pollfd *pfds = malloc(sizeof *pfds * fd_size);

    // Set up and get a listening socket
    listener = get_listener_socket();

    if (listener == -1) {
        fprintf(stderr, "error getting listening socket\n");
        exit(1);
    }

    // Add the listener to set
    pfds[0].fd = listener;
    pfds[0].events = POLLIN; // Report ready to read on incoming connection

    fd_count = 1; // For the listener

    // Main loop
    for (;;) {
        int poll_count = poll(pfds, fd_count, -1);

        if (poll_count == -1) {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for (int i = 0; i < fd_count; i++) {

            // Check if someone's ready to read
            if (pfds[i].revents & POLLIN) { // We got one!!

                if (pfds[i].fd == listener) {
                    // If listener is ready to read, handle new connection

                    addrlen = sizeof remote_addr;
                    new_fd = accept(listener,
                                    (struct sockaddr *) &remote_addr,
                                    &addrlen);

                    if (new_fd == -1) { // unable to accept
                        perror("accept");
                    } else {
                        add_to_poll_fds(&pfds, new_fd, &fd_count, &fd_size);

                        printf("pollserver: new connection from %s on "
                               "socket %d\n",
                               inet_ntop(remote_addr.ss_family,
                                         get_in_addr((struct sockaddr *) &remote_addr),
                                         remoteIP, INET6_ADDRSTRLEN),
                               new_fd);
                    }
                } else {
                    // If not the listener, we're just a regular client
                    int nbytes = recv(pfds[i].fd, buf, sizeof buf, 0);

                    int sender_fd = pfds[i].fd;

                    if (nbytes <= 0) {
                        // Got error or connection closed by client
                        if (nbytes == 0) {
                            // Connection closed
                            printf("pollserver: socket %d hung up\n", sender_fd);
                        } else {
                            perror("recv");
                        }

                        close(pfds[i].fd); // Bye!

                        del_from_pfds(pfds, i, &fd_count);

                    } else {
                        // We got some good data from a client

                        for (int j = 0; j < fd_count; j++) {
                            // Send to everyone!
                            int dest_fd = pfds[j].fd;

                            // Except the listener and ourselves
                            if (dest_fd != listener && dest_fd != sender_fd) {
                                if (send(dest_fd, buf, nbytes, 0) == -1) {
                                    perror("send");
                                }
                            }
                        }
                    }
                } // END handle data from client
            } // END got ready-to-read from poll()
        } // END looping through file descriptors
    } // END for(;;)--and you thought it would never end!

    return 0;
}
