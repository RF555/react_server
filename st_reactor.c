#include "reactor.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void *reactorRun(void *react) {
    if (react == NULL) {
        errno = EINVAL;
        fprintf(stderr, "reactorRun() failed: %s\n", strerror(EINVAL));
        return NULL;
    }

    reactor_t_ptr reactor = (reactor_t_ptr) react;

    while (reactor->running) {
        size_t size = 0, i = 0;
        reactor_node_ptr curr = reactor->head;

        while (curr != NULL) {
            size++;
            curr = curr->next;
        }

        curr = reactor->head;

        reactor->fds = (pollfd_t_ptr) calloc(size, sizeof(pollfd_t));

        if (reactor->fds == NULL) {
            fprintf(stderr, "reactorRun() failed: %s\n", strerror(errno));
            return NULL;
        }

        while (curr != NULL) {
            (*(reactor->fds + i)).fd = curr->fd;
            (*(reactor->fds + i)).events = POLLIN;

            curr = curr->next;
            i++;
        }

        int ret = poll(reactor->fds, i, -1);

        if (ret < 0) {
            fprintf(stderr, "poll() failed: %s\n", strerror(errno));
            free(reactor->fds);
            reactor->fds = NULL;
            return NULL;
        } else if (ret == 0) {
            fprintf(stdout, "poll() timed out.\n");
            free(reactor->fds);
            reactor->fds = NULL;
            continue;
        }

        for (i = 0; i < size; ++i) {
            if ((*(reactor->fds + i)).revents & POLLIN) {
                reactor_node_ptr curr = reactor->head;

                for (unsigned int j = 0; j < i; ++j)
                    curr = curr->next;

                void *handler_ret = curr->hdlr.handler((*(reactor->fds + i)).fd, reactor);

                if (handler_ret == NULL && (*(reactor->fds + i)).fd != reactor->head->fd) {
                    reactor_node_ptr curr_node = reactor->head;
                    reactor_node_ptr prev_node = NULL;

                    while (curr_node != NULL && curr_node->fd != (*(reactor->fds + i)).fd) {
                        prev_node = curr_node;
                        curr_node = curr_node->next;
                    }

                    prev_node->next = curr_node->next;

                    free(curr_node);
                }

                continue;
            } else if (((*(reactor->fds + i)).revents & POLLHUP || (*(reactor->fds + i)).revents & POLLNVAL ||
                        (*(reactor->fds + i)).revents & POLLERR) && (*(reactor->fds + i)).fd != reactor->head->fd) {
                reactor_node_ptr curr_node = reactor->head;
                reactor_node_ptr prev_node = NULL;

                while (curr_node != NULL && curr_node->fd != (*(reactor->fds + i)).fd) {
                    prev_node = curr_node;
                    curr_node = curr_node->next;
                }

                prev_node->next = curr_node->next;

                free(curr_node);
            }
        }

        free(reactor->fds);
        reactor->fds = NULL;
    }

    fprintf(stdout, "Reactor thread finished.\n");

    return reactor;
}

void *createReactor() {
    reactor_t_ptr react = NULL;

    fprintf(stdout, "Creating reactor...\n");

    if ((react = (reactor_t_ptr) malloc(sizeof(reactor_t))) == NULL) {
        fprintf(stderr, "malloc() failed: %s\n", strerror(errno));
        return NULL;
    }

    react->thread = 0;
    react->head = NULL;
    react->fds = NULL;
    react->running = false;

    fprintf(stdout, "Reactor created.\n");

    return react;
}

void startReactor(void *react) {
    if (react == NULL) {
        fprintf(stderr, "startReactor() failed: %s\n", strerror(EINVAL));
        return;
    }

    reactor_t_ptr reactor = (reactor_t_ptr) react;

    if (reactor->head == NULL) {
        fprintf(stderr, "Tried to start a reactor without registered file descriptors.\n");
        return;
    } else if (reactor->running) {
        fprintf(stderr, "Tried to start a reactor that's already running.\n");
        return;
    }

    fprintf(stdout, "Starting reactor thread...\n");

    reactor->running = true;

    int ret_val = pthread_create(&reactor->thread, NULL, reactorRun, react);

    if (ret_val != 0) {
        fprintf(stderr, "pthread_create() failed: %s\n", strerror(ret_val));
        reactor->running = false;
        reactor->thread = 0;
        return;
    }

    fprintf(stdout, "Reactor thread started.\n");
}

void stopReactor(void *react) {
    if (react == NULL) {
        fprintf(stderr, "stopReactor() failed: %s\n", strerror(EINVAL));
        return;
    }

    reactor_t_ptr reactor = (reactor_t_ptr) react;
    void *ret = NULL;

    if (!reactor->running) {
        fprintf(stderr, "Tried to stop a reactor that's not currently running.\n");
        return;
    }

    fprintf(stdout, "Stopping reactor thread gracefully...\n");

    reactor->running = false;

    /*
     * In case the thread is blocked on poll(), we ensure that the thread
     * is cancelled by joining and detaching it.
     * This prevents memory leaks.
    */
    int ret_val = pthread_cancel(reactor->thread);

    if (ret_val != 0) {
        fprintf(stderr, "pthread_cancel() failed: %s\n", strerror(ret_val));
        return;
    }

    ret_val = pthread_join(reactor->thread, &ret);

    if (ret_val != 0) {
        fprintf(stderr, "pthread_join() failed: %s\n", strerror(ret_val));
        return;
    }

    if (ret == NULL) {
        fprintf(stderr, "Reactor thread fatal error: %s", strerror(errno));
        return;
    }

    // Free the reactor's file descriptors.
    if (reactor->fds != NULL) {
        free(reactor->fds);
        reactor->fds = NULL;
    }

    // Reset reactor pthread.
    reactor->thread = 0;

    fprintf(stdout, "Reactor thread stopped.\n");
}

void addFd(void *react, int fd, handler_t handler) {
    if (react == NULL || handler == NULL || fd < 0 || fcntl(fd, F_GETFL) == -1 || errno == EBADF) {
        fprintf(stderr, "addFd() failed: %s\n", strerror(EINVAL));
        return;
    }

    fprintf(stdout, "Adding file descriptor %d to the list.\n", fd);

    reactor_t_ptr reactor = (reactor_t_ptr) react;
    reactor_node_ptr node = (reactor_node_ptr) malloc(sizeof(reactor_node));

    if (node == NULL) {
        fprintf(stderr, "malloc() failed: %s\n", strerror(errno));
        return;
    }

    node->fd = fd;
    node->hdlr.handler = handler;
    node->next = NULL;

    if (reactor->head == NULL)
        reactor->head = node;

    else {
        reactor_node_ptr curr = reactor->head;

        while (curr->next != NULL)
            curr = curr->next;

        curr->next = node;
    }

    fprintf(stdout, "Successfuly added file descriptor %d to the list, function handler address: %p.\n",
            fd, node->hdlr.handler_ptr);
}

void WaitFor(void *react) {
    if (react == NULL) {
        fprintf(stderr, "WaitFor() failed: %s\n", strerror(EINVAL));
        return;
    }

    reactor_t_ptr reactor = (reactor_t_ptr) react;
    void *ret = NULL;

    if (!reactor->running)
        return;

    fprintf(stdout, "Reactor thread joined.\n");
    printf("1pthread_join() was successful!\n");


    int ret_val = pthread_join(reactor->thread, &ret);
    printf("2pthread_join() was successful!\n");

    if (ret_val != 0) {
        fprintf(stderr, "pthread_join() failed: %s\n", strerror(ret_val));
        return;
    }

    if (ret == NULL)
        fprintf(stderr, "Reactor thread fatal error: %s", strerror(errno));

    printf("3pthread_join() was successful!\n");
}