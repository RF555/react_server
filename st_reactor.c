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
        fprintf(stderr, "reactor_run() failed: %s\n", strerror(EINVAL));
        return NULL;
    }

    reactor_struct_ptr reactor = (reactor_struct_ptr) reactor_ptr;

    while (reactor->is_running) {
        size_t n_fds = 0, fd_count = 0;
        fd_node_ptr curr_fd = reactor->src;

        while (curr != NULL) { // count how many fd's the reacor has
            ++n_fds;
            curr_fd = curr_fd->next_fd;
        }
        curr_fd = reactor->src;

        // allocate enough memory for all the fds
        reactor->fds_ptr = (poll_fd_ptr) calloc(size, sizeof(poll_fd));

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
        for (int i = 0; i < fd_count; ++i) {
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
    reactor->is_running = NOT_RUNNING;
    fprintf(stdout, "Reactor created.\n");
    return reactor;
}

















