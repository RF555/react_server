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
    fprintf(stderr, "Called reactorRun()\n");
    if (reactor_ptr == NULL) {
        fprintf(stderr, "%sreactorRun() failed: %s\n", ERROR_PRINT, RESET_COLOR);
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
            fprintf(stderr, "%sreactorRun() failed.%s\n", ERROR_PRINT, RESET_COLOR);
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
            fprintf(stderr, "%spoll() failed.%s\n", ERROR_PRINT, RESET_COLOR);
            free(reactor->fds_ptr);
            reactor->fds_ptr = NULL;
            return NULL;
        } else if (poll_count == 0) { // poll timed out
            fprintf(stdout, "%spoll() timed out.%s\n", ERROR_PRINT, RESET_COLOR);
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
    fprintf(stdout, "Called createReactor()\n");
    reactor_struct_ptr reactor = NULL;
    fprintf(stdout, "\tCreating reactor...\n");
    if ((reactor = (reactor_struct_ptr) malloc(sizeof(reactor_struct))) == NULL) {
        fprintf(stderr, "%smalloc() failed.%s\n", ERROR_PRINT, RESET_COLOR);
        return NULL;
    }
    reactor->my_thread = 0;
    reactor->src = NULL;
    reactor->fds_ptr = NULL;
    reactor->is_running = NO;
    fprintf(stdout, "\tReactor created.\n");
    return reactor;
}


void startReactor(void *reactor_ptr) {
    fprintf(stderr, "Called startReactor()\n");
    if (reactor_ptr == NULL) {
        fprintf(stderr, "%sstartReactor() failed.%s\n", ERROR_PRINT, RESET_COLOR);
        return;
    }

    reactor_struct_ptr reactor = (reactor_struct_ptr) reactor_ptr;
    if (reactor->src == NULL) {
        fprintf(stderr, "%sReactor has no file descriptor!%s\n", ERROR_PRINT, RESET_COLOR);
        return;
    } else if (reactor->is_running == YES) {
        fprintf(stderr, "%sReactor is already running!%s\n", ERROR_PRINT, RESET_COLOR);
        return;
    }

    fprintf(stdout, "\tStarting reactor thread...\n");

    reactor->is_running = YES;

    int created_thread = pthread_create(&reactor->my_thread, NULL, reactorRun, reactor_ptr);

    if (created_thread != 0) {
        fprintf(stderr, "%spthread_create() failed.%s\n", ERROR_PRINT, RESET_COLOR);
        reactor->is_running = NO;
        reactor->my_thread = 0;
        return;
    }
    fprintf(stdout, "\tReactor thread started.\n");
}


void stopReactor(void *reactor_ptr) {
    fprintf(stderr, "Called stopReactor()\n");
    if (reactor_ptr == NULL) {
        fprintf(stderr, "%sstopReactor() failed.%s\n", ERROR_PRINT, RESET_COLOR);
        return;
    }
    reactor_struct_ptr reactor = (reactor_struct_ptr) reactor_ptr;
    void *temp_thread = NULL;

    if (reactor->is_running == NO) {
        fprintf(stderr, "%sReactor is not running!%s\n", ERROR_PRINT, RESET_COLOR);
        return;
    }
    fprintf(stdout, "\tStopping reactor thread...\n");

    reactor->is_running = NO;

    int canceled_thread = pthread_cancel(reactor->my_thread);

    if (canceled_thread != 0) {
        fprintf(stderr, "%spthread_cancel() failed.%s\n", ERROR_PRINT, RESET_COLOR);
        return;
    }

    canceled_thread = pthread_join(reactor->my_thread, &temp_thread);

    if (canceled_thread != 0) {
        fprintf(stderr, "%spthread_join() failed.%s\n", ERROR_PRINT, RESET_COLOR);
        return;
    }
    if (temp_thread == NULL) {
        fprintf(stderr, "%sReactor thread error.%s\n", ERROR_PRINT, RESET_COLOR);
        return;
    }

    if (reactor->fds_ptr != NULL) {
        free(reactor->fds_ptr);
        reactor->fds_ptr = NULL;
    }

    reactor->my_thread = 0;

    fprintf(stdout, "\tReactor thread stopped.\n");
}

void addFd(void *reactor_ptr, int fd, handler_t handler) {
    fprintf(stderr, "Called addFd()\n");
    if (reactor_ptr == NULL || handler == NULL || fd < 0 || fcntl(fd, F_GETFL) == -1 || errno == EBADF) {
        fprintf(stderr, "%saddFd() failed.%s\n", ERROR_PRINT, RESET_COLOR);
        return;
    }
    fprintf(stdout, "\tAdding file descriptor %d.\n", fd);

    reactor_struct_ptr reactor = (reactor_struct_ptr) reactor_ptr;
    fd_node_ptr new_node = (fd_node_ptr) malloc(sizeof(fd_node));
    if (new_node == NULL) {
        fprintf(stderr, "%smalloc() failed.%s\n", ERROR_PRINT, RESET_COLOR);
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
    fprintf(stdout, "\tSuccessfully added file descriptor %d.\n", fd);
}


void WaitFor(void *reactor_ptr) {
    fprintf(stderr, "Called WaitFor()\n");
    if (reactor_ptr == NULL) {
        fprintf(stderr, "%sWaitFor() failed.%s\n", ERROR_PRINT, RESET_COLOR);
        return;
    }

    reactor_struct_ptr reactor = (reactor_struct_ptr) reactor_ptr;
    void *temp_thread = NULL;

    if (reactor->is_running == NO) {
        return;
    }

    fprintf(stdout, "\tReactor thread joined.\n");

    int joined_thread = pthread_join(reactor->my_thread, &temp_thread);

    if (joined_thread != 0) {
        fprintf(stderr, "%spthread_join() failed.%s\n", ERROR_PRINT, RESET_COLOR);
        return;
    }

    if (temp_thread == NULL) {
        fprintf(stderr, "%sReactor thread fatal error.%s", ERROR_PRINT, RESET_COLOR);
    }
}

