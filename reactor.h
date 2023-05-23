#ifndef _REACTOR_H
#define _REACTOR_H


#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <poll.h>

/**
 * @brief Default port the server listens to (same as in Beej's guide).
 */
#define DEFAULT_PORT 9034

/**
 * @brief Maximum number of clients connection to the server.
 */
#define MAX_CLIENT 20

/**
 * @brief Maximum input size.
 */
#define MAX_BUFFER 1024

typedef enum RUNNING {
    IS_RUNNING = 0, NOT_RUNNING = -1
} _running;

/**
 * @brief Handler for a File Descriptor.
 * @param fd File Descriptor.
 * @param reactor Pointer to the reactor.
 * @return Pointer of the return from the handler.
 */
typedef void *(*handler_t)(int fd, void *react_ptr);

/**
 * @brief A node representing a file descriptor in the reactor.
 */
typedef struct _fd_reactor_node fd_reactor_node, *fd_reactor_node_ptr;

/**
 * @brief Reactor represented as a linked-list of fds.
 */
typedef struct _reactor_struct reactor_struct, *reactor_struct_ptr;

typedef struct pollfd pollfd_t, *pollfd_t_ptr; // Redefine 'struct pollfd' declared in 'poll.h'


struct _fd_reactor_node {
    int fd; // File Descriptor.
    handler_t handler; // Handler of the fd.
    void *handler_ptr; // Pointer to handler of the fd.
    fd_reactor_node_ptr next_fd; // Pointer to the next fd node.
};

struct _reactor_struct {
    pthread_t reactor_thread; // Thread the reactor runs on.
    fd_reactor_node_ptr src; // First fd of the reactor's list (always listen socket).
    pollfd_t_ptr fds_ptr; // Pointer to the array of pollfd's.
    bool is_running; // Enum indicating the reactors state.
};


/**
 * @brief Creates a reactor as reactor_struct structure.
 * @return Pointer to the new reactor.
 */
void *createReactor();

/**
 * @brief Start the reactor in a new thread.
 * @param reactor_ptr Pointer to an already generated reactor.
 */
void startReactor(void *react_ptr);

/**
 * @brief Stop the reactor.
 * @param reactor_ptr Pointer to the reactor.
 */
void stopReactor(void *react_ptr);

/**
 * @brief Add a file descriptor.
 * @param reactor_ptr Pointer to the reactor.
 * @param fd File Descriptor.
 * @param handler Pointer to the handler function.
 */
void addFd(void *react_ptr, int fd, handler_t handler);

/**
 * @brief Wait for the reactor.
 * @param reactor_ptr Pointer to the reactor.
 */
void WaitFor(void *react_ptr);


/**
 * @brief Signal handler for SIGINT (ctl-C).
 */
void signal_handler();

/**
 * @brief
 * @param fd The client socket descriptor.
 * @param reactor_ptr Pointer to the reactor
 * @return Pointer to the reactor.
 */
void *client_handler(int fd, void *react_ptr);

/**
 * @brief
 * @param fd The server socket descriptor.
 * @param reactor_ptr Pointer to the reactor
 * @return Pointer to the reactor.
 */
void *server_handler(int fd, void *react_ptr);

#endif