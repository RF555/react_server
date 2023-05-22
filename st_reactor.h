#ifndef ST_REACTOR_H
#define ST_REACTOR_H

#include <stdio.h>
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
#define  MAX_CLIENT 20
/**
 * @brief Maximum input size.
 */
#define MAX_INPUT 1024

typedef enum RUNNING _running{IS_RUNNING = 0, NOT_RUNNING = -1};

/**
 * @brief Handler for a File Descriptor.
 * @param fd File Descriptor.
 * @param reactor Pointer to the reactor.
 * @return Pointer of the return from the handler.
 */
typedef void *(*handler_func_ptr)(int fd, void *reactor_ptr);

/**
 * @brief A node representing a file descriptor in the reactor.
 */
typedef struct _fd_node fd_node, *fd_node_ptr;

/**
 * @brief Reactor represented as a linked-list of fds.
 */
typedef struct _reactor reactor, *reactor_ptr;

typedef struct pollfd poll_fd, *poll_fd_ptr; // Redefine 'struct pollfd' declared in 'poll.h'.


struct _fd_node {
    int fd; // File Descriptor.
    handler_func_ptr handler; // Handler of the fd.
    void *handler_ptr; // Pointer to handler of the fd.
    fd_node_ptr next_fd; // Pointer to the next fd node.
};

struct _reactor {
    pthread_t my_thread; // Thread the reactor runs on.
    fd_node_ptr src; // First fd of the reactor's list (always listen socket).
    poll_fd_ptr fds_ptr; // Pointer to the array of pollfd's.
    _running running; // Enum indicating the reactors state.
};


/**
 * @brief Generate a reactor as a _reactor structure.
 * @return Pointer to the new reactor.
 */
void *generate_reactor();

/**
 * @brief Start the reactor in a new thread.
 * @param reactor_ptr Pointer to an already generated reactor.
 */
void start_reactor(void *reactor_ptr);

/**
 * @brief Stop the reactor.
 * @param reactor_ptr Pointer to the reactor.
 */
void stop_reactor(void *reactor_ptr);


/**
 * @brief Add a file descriptor.
 * @param reactor_ptr Pointer to the reactor.
 * @param fd File Descriptor.
 * @param handler Pointer to the handler function.
 */
void add_fd(void *reactor_ptr, int fd, handler_func_ptr handler);


/**
 * @brief Wait for the reactor.
 * @param reactor_ptr Pointer to the reactor.
 */
void wait_for(void *reactor_ptr);

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
void *client_handler(int fd, void *reactor_ptr);

/**
 * @brief
 * @param fd The server socket descriptor.
 * @param reactor_ptr Pointer to the reactor
 * @return Pointer to the reactor.
 */
void *new_client(int fd, void *reactor_ptr);


#endif
