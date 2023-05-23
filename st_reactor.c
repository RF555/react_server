#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "st_reactor.h"

/**
 * @brief
 * @param reactor_ptr
 * @return
 */
void *reactorRun(void *reactor_ptr) {
    if (reactor_ptr == NULL) {
        errno = EINVAL;
        fprintf(stderr, "reactorRun() failed: %s\n", strerror(EINVAL));
        return NULL;
    }

    reactor_struct_ptr reactor = (reactor_struct_ptr) reactor_ptr;

    while (reactor->is_running == YES) {
        size_t n_fds = 0, fd_count = 0;
        fd_node_ptr curr_fd = reactor->src;

        while (curr_fd != NULL) { // count how many fd's the reacor has
            ++n_fds;
            curr_fd = curr_fd->next_fd;
        }
        curr_fd = reactor->src;

        // allocate enough memory for all the fds
        reactor->fds_ptr = (poll_fd_ptr) calloc(n_fds, sizeof(poll_fd));

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
        for (size_t i = 0; i < fd_count; ++i) {
            if ((*(reactor->fds_ptr + i)).revents & POLLIN) {// Check if fd is ready to read
                curr_fd = reactor->src;

                for (unsigned int j = 0; j < i; ++j) { // set curr_fd to be the ith fd
                    curr_fd = curr_fd->next_fd;
                }

                void *handler_func_poll = curr_fd->handler((*(reactor->fds_ptr + i)).fd,
                                                           reactor); // set the handler function

                if (handler_func_poll == NULL && (*(reactor->fds_ptr + i)).fd != reactor->src->fd) {
                    /* if unable to poll *AND* the fd is not the listening socket:
                     * free and remove this fd_node */
                    fd_node_ptr curr_node = reactor->src;
                    fd_node_ptr prev_node = NULL;

                    while (curr_node != NULL && curr_node->fd != (*(reactor->fds_ptr + i)).fd) {
                        prev_node = curr_node;
                        curr_node = curr_node->next_fd;
                    }
                    prev_node->next_fd = curr_node->next_fd; // skip the current fd_node
                    free(curr_node);
                }
                continue;
            } else if (((*(reactor->fds_ptr + i)).revents & POLLHUP
                        || (*(reactor->fds_ptr + i)).revents & POLLNVAL
                        || (*(reactor->fds_ptr + i)).revents & POLLERR)
                       && ((*(reactor->fds_ptr + i)).fd != reactor->src->fd)) {
                /* if one of these 3:
                 *          1. Hung up
                 *          2. Invalid polling request
                 *          3. Error condition
                 *    ---happened *AND* the current fd is not the src fd (listening socket)
                 * ---> free and remove this fd_node */
                fd_node_ptr curr_node = reactor->src;
                fd_node_ptr prev_node = NULL;

                while (curr_node != NULL && curr_node->fd != (*(reactor->fds_ptr + i)).fd) {
                    prev_node = curr_node;
                    curr_node = curr_node->next_fd;
                }
                prev_node->next_fd = curr_node->next_fd; // skip the current fd_node
                free(curr_node);
            }
        }

        free(reactor->fds_ptr);
        reactor->fds_ptr = NULL;
    }
    fprintf(stdout, "Reactor thread finished.\n");
    return reactor;
}


void *createReactor() {
    reactor_struct_ptr reactor = NULL;
    fprintf(stdout, "Called createReactor()\nCreating reactor...\n");
    if ((reactor = (reactor_struct_ptr) malloc(sizeof(reactor_struct))) == NULL) {
        fprintf(stderr, "malloc() failed: %s\n", strerror(errno));
        return NULL;
    }
    reactor->my_thread = 0;
    reactor->src = NULL;
    reactor->fds_ptr = NULL;
    reactor->is_running = NO;
    fprintf(stdout, "Reactor created.\n");
    return reactor;
}


void startReactor(void *reactor_ptr) {
    if (reactor_ptr == NULL) {
        fprintf(stderr, "startReactor() failed: %s\n", strerror(EINVAL));
        return;
    }

    reactor_struct_ptr reactor = (reactor_struct_ptr) reactor_ptr;
    if (reactor->src == NULL) {
        fprintf(stderr, "Tried to start a reactor without registered file descriptors.\n");
        return;
    } else if (reactor->is_running == YES) {
        fprintf(stderr, "Tried to start a reactor that's already running.\n");
        return;
    }

    fprintf(stdout, "Starting reactor thread...\n");

    reactor->is_running = YES;

    int created_thread = pthread_create(&reactor->my_thread, NULL, reactorRun, reactor_ptr);

    if (created_thread != 0) {
        fprintf(stderr, "pthread_create() failed: %s\n", strerror(created_thread));
        reactor->is_running = NO;
        reactor->my_thread = 0;
        return;
    }
    fprintf(stdout, "Reactor thread started.\n");
}


void stopReactor(void *reactor_ptr) {
    if (reactor_ptr == NULL) {
        fprintf(stderr, "stopReactor() failed: %s\n", strerror(EINVAL));
        return;
    }
    reactor_struct_ptr reactor = (reactor_struct_ptr) reactor_ptr;
    void *temp_thread = NULL;

    if (reactor->is_running == NO) {
        fprintf(stderr, "Tried to stop a reactor that's not currently running.\n");
        return;
    }
    fprintf(stdout, "Stopping reactor thread gracefully...\n");

    reactor->is_running = NO;

    int canceled_thread = pthread_cancel(reactor->my_thread);

    if (canceled_thread != 0) {
        fprintf(stderr, "pthread_cancel() failed: %s\n", strerror(canceled_thread));
        return;
    }

    canceled_thread = pthread_join(reactor->my_thread, &temp_thread);

    if (canceled_thread != 0) {
        fprintf(stderr, "pthread_join() failed: %s\n", strerror(canceled_thread));
        return;
    }
    if (temp_thread == NULL) {
        fprintf(stderr, "Reactor thread fatal error: %s", strerror(errno));
        return;
    }

    if (reactor->fds_ptr != NULL) {
        free(reactor->fds_ptr);
        reactor->fds_ptr = NULL;
    }

    reactor->my_thread = 0;

    fprintf(stdout, "Reactor thread stopped.\n");
}

void addFd(void *reactor_ptr, int fd, handler_t handler) {
    if (reactor_ptr == NULL || handler == NULL || fd < 0 || fcntl(fd, F_GETFL) == -1 || errno == EBADF) {
        fprintf(stderr, "addFd() failed: %s\n", strerror(EINVAL));
        return;
    }
    fprintf(stdout, "Adding file descriptor %d to the list.\n", fd);

    reactor_struct_ptr reactor = (reactor_struct_ptr) reactor_ptr;
    fd_node_ptr new_node = (fd_node_ptr) malloc(sizeof(fd_node));
    if (new_node == NULL) {
        fprintf(stderr, "malloc() failed: %s\n", strerror(errno));
        return;
    }

    new_node->fd = fd;
    new_node->handler = handler;
    new_node->next_fd = NULL;

    if (reactor->src == NULL) {
        reactor->src = new_node;
    } else {
        fd_node_ptr temp_node = reactor->src;

        while (temp_node->next_fd != NULL) {
            temp_node = temp_node->next_fd;
        }
        temp_node->next_fd = new_node;
    }
    fprintf(stdout, "Successfuly added file descriptor %d to the list.\n", fd);
}


void WaitFor(void *reactor_ptr) {
    if (reactor_ptr == NULL) {
        fprintf(stderr, "WaitFor() failed: %s\n", strerror(EINVAL));
        return;
    }

    reactor_struct_ptr reactor = (reactor_struct_ptr) reactor_ptr;
    void *temp_thread = NULL;

    if (reactor->is_running == NO) {
        return;
    }

    fprintf(stdout, "Reactor thread joined.\n");

    int joined_thread = pthread_join(reactor->my_thread, &temp_thread);

    if (joined_thread != 0) {
        fprintf(stderr, "pthread_join() failed: %s\n", strerror(joined_thread));
        return;
    }

    if (temp_thread == NULL) {
        fprintf(stderr, "Reactor thread fatal error: %s", strerror(errno));
    }
}

