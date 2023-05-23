#include "reactor.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void *reactorRun(void *react_ptr) {
    if (react_ptr == NULL) {
        errno = EINVAL;
        fprintf(stderr, "reactorRun() failed: %s\n", strerror(EINVAL));
        return NULL;
    }

    reactor_struct_ptr reactor = (reactor_struct_ptr) react_ptr;

    while (reactor->is_running == YES) {
        size_t fds_size = 0;
        size_t fd_count = 0;
        fd_reactor_node_ptr curr_fd = reactor->src;

        while (curr_fd != NULL) { // count how many fd's the reactor have
            ++fds_size;
            curr_fd = curr_fd->next_fd;
        }

        curr_fd = reactor->src;

        // allocate enough memory for all the fds
        reactor->fds_ptr = (pollfd_t_ptr) calloc(fds_size, sizeof(pollfd_t));

        if (reactor->fds_ptr == NULL) {
            fprintf(stderr, "reactorRun() failed: %s\n", strerror(errno));
            return NULL;
        }

        while (curr_fd != NULL) { // loop over all fds
            (*(reactor->fds_ptr + fd_count)).fd = curr_fd->fd; // set the ith fd of the reactor to be the current fd
            (*(reactor->fds_ptr + fd_count)).events = POLLIN; // set to alert when ready to recv() on this socket

            curr_fd = curr_fd->next_fd;
            ++fd_count;
        }

        int poll_count = poll(reactor->fds_ptr, fd_count, -1);

        if (poll_count < 0) { // poll failed
            fprintf(stderr, "poll() failed: %s\n", strerror(errno));
            free(reactor->fds_ptr);
            reactor->fds_ptr = NULL;
            return NULL;
        } else if (poll_count == 0) { // poll timed out
            fprintf(stdout, "poll() timed out.\n");
            free(reactor->fds_ptr);
            reactor->fds_ptr = NULL;
            continue;
        }

        // Run through the existing connections looking for data to read
        for (size_t i = 0; i < fds_size; ++i) {
            if ((*(reactor->fds_ptr + i)).revents & POLLIN) { // Check if fd is ready to read
                fd_reactor_node_ptr curr_fd = reactor->src;

                for (unsigned int j = 0; j < i; ++j) { // set curr_fd to be the ith fd
                    curr_fd = curr_fd->next_fd;
                }

                void *handler_func_poll = curr_fd->handler((*(reactor->fds_ptr + i)).fd, reactor);
                if (handler_func_poll == NULL && (*(reactor->fds_ptr + i)).fd != reactor->src->fd) {
                    /* if unable to poll *AND* the fd is not the listening socket:
                     * free and remove this fd_node */
                    fd_reactor_node_ptr curr_fd = reactor->src;
                    fd_reactor_node_ptr prev_fd = NULL;

                    while (curr_fd != NULL && curr_fd->fd != (*(reactor->fds_ptr + i)).fd) {
                        prev_fd = curr_fd;
                        curr_fd = curr_fd->next_fd;
                    }
                    prev_fd->next_fd = curr_fd->next_fd; // skip the current fd_node

                    free(curr_fd);
                }
                continue;

            } else if (((*(reactor->fds_ptr + i)).revents & POLLHUP
                        || (*(reactor->fds_ptr + i)).revents & POLLNVAL
                        || (*(reactor->fds_ptr + i)).revents & POLLERR)
                       && (*(reactor->fds_ptr + i)).fd != reactor->src->fd) {
                /* if one of these 3:
                 *          1. Hung up
                 *          2. Invalid polling request
                 *          3. Error condition
                 *    ---happened *AND* the current fd is not the src fd (listening socket)
                 * ---> free and remove this fd_node */

                fd_reactor_node_ptr curr_fd = reactor->src;
                fd_reactor_node_ptr prev_fd = NULL;

                while (curr_fd != NULL && curr_fd->fd != (*(reactor->fds_ptr + i)).fd) {
                    prev_fd = curr_fd;
                    curr_fd = curr_fd->next_fd;
                }

                prev_fd->next_fd = curr_fd->next_fd;

                free(curr_fd);
            }
        }

        free(reactor->fds_ptr);
        reactor->fds_ptr = NULL;
    }

    fprintf(stdout, "Reactor thread finished.\n");

    return reactor;
}

void *createReactor() {
    reactor_struct_ptr react_ptr = NULL;

    fprintf(stdout, "Creating reactor...\n");

    if ((react_ptr = (reactor_struct_ptr) malloc(sizeof(reactor_struct))) == NULL) {
        fprintf(stderr, "malloc() failed: %s\n", strerror(errno));
        return NULL;
    }

    react_ptr->reactor_thread = 0;
    react_ptr->src = NULL;
    react_ptr->fds_ptr = NULL;
    react_ptr->is_running = NO;

    fprintf(stdout, "Reactor created.\n");

    return react_ptr;
}

void startReactor(void *react_ptr) {
    if (react_ptr == NULL) {
        fprintf(stderr, "startReactor() failed: %s\n", strerror(EINVAL));
        return;
    }

    reactor_struct_ptr reactor = (reactor_struct_ptr) react_ptr;

    if (reactor->src == NULL) {
        fprintf(stderr, "Tried to start a reactor without registered file descriptors.\n");
        return;
    } else if (reactor->is_running == YES) {
        fprintf(stderr, "Tried to start a reactor that's already running.\n");
        return;
    }

    fprintf(stdout, "Starting reactor thread...\n");

    reactor->is_running = YES;

    int create_return_value = pthread_create(&reactor->reactor_thread, NULL, reactorRun, react_ptr);

    if (create_return_value != 0) {
        fprintf(stderr, "pthread_create() failed: %s\n", strerror(create_return_value));
        reactor->is_running = NO;
        reactor->reactor_thread = 0;
        return;
    }

    fprintf(stdout, "Reactor thread started.\n");
}

void stopReactor(void *react_ptr) {
    if (react_ptr == NULL) {
        fprintf(stderr, "stopReactor() failed: %s\n", strerror(EINVAL));
        return;
    }

    reactor_struct_ptr reactor = (reactor_struct_ptr) react_ptr;
    void *return_value_ptr = NULL;

    if (reactor->is_running == NO) {
        fprintf(stderr, "Tried to stop a reactor that's not currently running.\n");
        return;
    }

    fprintf(stdout, "Stopping reactor thread gracefully...\n");

    reactor->is_running = NO;

    /*
     * In case the thread is blocked on poll(), we ensure that the thread
     * is cancelled by joining and detaching it.
     * This prevents memory leaks.
    */
    int cancel_return_value = pthread_cancel(reactor->reactor_thread);
    if (cancel_return_value != 0) {
        fprintf(stderr, "pthread_cancel() failed: %s\n", strerror(cancel_return_value));
        return;
    }

    int join_return_value = pthread_join(reactor->reactor_thread, &return_value_ptr);
    if (join_return_value != 0) {
        fprintf(stderr, "pthread_join() failed: %s\n", strerror(join_return_value));
        return;
    }

    if (return_value_ptr == NULL) {
        fprintf(stderr, "Reactor thread fatal error: %s", strerror(errno));
        return;
    }

    // Free the reactor's file descriptors.
    if (reactor->fds_ptr != NULL) {
        free(reactor->fds_ptr);
        reactor->fds_ptr = NULL;
    }

    // Reset reactor pthread.
    reactor->reactor_thread = 0;

    fprintf(stdout, "Reactor thread stopped.\n");
}

void addFd(void *react_ptr, int fd, handler_t handler) {
    if (react_ptr == NULL || handler == NULL || fd < 0 || fcntl(fd, F_GETFL) == -1 || errno == EBADF) {
        fprintf(stderr, "addFd() failed: %s\n", strerror(EINVAL));
        return;
    }

    fprintf(stdout, "Adding file descriptor %d to the list.\n", fd);

    reactor_struct_ptr reactor = (reactor_struct_ptr) react_ptr;
    fd_reactor_node_ptr new_fd_node = (fd_reactor_node_ptr) malloc(sizeof(fd_reactor_node));

    if (new_fd_node == NULL) {
        fprintf(stderr, "malloc() failed: %s\n", strerror(errno));
        return;
    }

    new_fd_node->fd = fd;
    new_fd_node->handler = handler;
    new_fd_node->handler_ptr = &handler;
    new_fd_node->next_fd = NULL;

    if (reactor->src == NULL) {
        reactor->src = new_fd_node;
    } else {
        fd_reactor_node_ptr curr_fd = reactor->src;

        while (curr_fd->next_fd != NULL) {
            curr_fd = curr_fd->next_fd;
        }
        curr_fd->next_fd = new_fd_node;
    }

    fprintf(stdout, "Successfuly added file descriptor %d to the list, function handler address: %p.\n",
            fd, new_fd_node->handler_ptr);
}

void WaitFor(void *react_ptr) {
    if (react_ptr == NULL) {
        fprintf(stderr, "WaitFor() failed: %s\n", strerror(EINVAL));
        return;
    }

    reactor_struct_ptr reactor = (reactor_struct_ptr) react_ptr;
    void *return_value_ptr = NULL;

    if (reactor->is_running == NO)
        return;

    fprintf(stdout, "Reactor thread joined.\n");
    printf("1pthread_join() was successful!\n");


    int join_return_value = pthread_join(reactor->reactor_thread, &return_value_ptr);
    printf("2pthread_join() was successful!\n");

    if (join_return_value != 0) {
        fprintf(stderr, "pthread_join() failed: %s\n", strerror(join_return_value));
        return;
    }

    if (return_value_ptr == NULL)
        fprintf(stderr, "Reactor thread fatal error: %s", strerror(errno));

    printf("3pthread_join() was successful!\n");
}