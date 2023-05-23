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

#ifndef _REACTOR_H
#define _REACTOR_H

/*
 * Include this file before including system headers.  By default, with
 * C99 support from the compiler, it requests POSIX 2001 support.  With
 * C89 support only, it requests POSIX 1997 support.  Override the
 * default behaviour by setting either _XOPEN_SOURCE or _POSIX_C_SOURCE.
*/
#if !defined(_XOPEN_SOURCE) && !defined(_POSIX_C_SOURCE)
#if __STDC_VERSION__ >= 199901L
#define _XOPEN_SOURCE 600   /* SUS v3, POSIX 1003.1 2004 (POSIX 2001 + Corrigenda) */
#else
#define _XOPEN_SOURCE 500   /* SUS v2, POSIX 1003.1 1997 */
#endif /* __STDC_VERSION__ */
#endif /* !_XOPEN_SOURCE && !_POSIX_C_SOURCE */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <poll.h>


/********************/
/* Settings Section */
/********************/

/*
 * @brief The port on which the server listens.
 * @note The default port is 9034.
*/
#define SERVER_PORT        9034

/*
 * @brief The maximum number of clients that can connect to the server.
 * @note The default number is 16384 clients.
 * @note For alot of OSes this is above the hard-limit.
*/
#define MAX_QUEUE            16384

/*
 * @brief The maximum number of bytes that can be read from a socket.
 * @note The default number is 1024 bytes.
*/
#define MAX_BUFFER            1024

/*
 * @brief Defines the timeout for the poll() function.
 * @note The default timeout is -1.
 * @note A timeout of 0 means that poll() will return immediately.
 * @note A timeout of -1 means that poll() will wait forever.
*/
#define POLL_TIMEOUT        -1


/************************/
/* Messages definitions */
/************************/

// Prefixes

// Colored prefix for error messages.
#define C_PREFIX_ERROR        "\033[0;31m[ERROR]\033[0;37m"

// Colored prefix for warning messages.
#define C_PREFIX_WARNING    "\033[0;33m[WARNING]\033[0;37m"

// Colored prefix for infomation messages.
#define C_PREFIX_INFO        "\033[0;35m[INFO]\033[0;37m"

// Colored prefix for client messages.
#define C_PREFIX_MESSAGE    "\033[0;32m[MESSAGE]\033[0;37m"

// Messages

// Information and license message that's printing in the beggining of the program.
#define C_INFO_LICENSE    "\t\t\t\033[0;36mReactor Server\n" \
                            "\tCopyright (C) 2023  Roy Simanovich and Linor Ronen\n" \
                            "\tThis program comes with ABSOLUTELY NO WARRANTY.\n" \
                            "\tThis is free software, and you are welcome to redistribute it\n" \
                            "\tunder certain conditions; see `LICENSE' for details.\033[0;37m\n\n"

// Macro to cleanup the current line.
#define MACRO_CLEANUP        "\33[2K\r\033"


/********************/
/* Typedefs Section */
/********************/

/*
 * @brief A handler function for a file descriptor.
 * @param fd The file descriptor.
 * @param react Pointer to the reactor object.
 * @return A pointer to something that the handler may return.
 * @note Returning NULL means something went wrong with the file descriptor, and as a result,
 * 			the reactor will automaticly remove the problamtic file descriptor from the list.
*/
typedef void *(*handler_t)(int fd, void *react);

/*
 * @brief A node in the reactor linked list.
 */
typedef struct _reactor_node reactor_node, *reactor_node_ptr;

/*
 * @brief A reactor object - a linked list of file descriptors and their handlers.
 */
typedef struct _reactor_t reactor_t, *reactor_t_ptr;

/*
 * @brief A pollfd object, used to poll the file descriptors.
*/
typedef struct pollfd pollfd_t, *pollfd_t_ptr;


/**********************/
/* Structures Section */
/**********************/

/*
 * @brief A node in the reactor linked list.
 */
struct _reactor_node {
    /*
     * @brief The file descriptor.
     * @note The first node is always the listening socket.
    */
    int fd;

    /*
     * @brief The file descriptor's handler union.
     * @note The union is used to allow the handler to be printed as a generic pointer,
     * 			with the compiler's flag -Wpedantic.
    */
    union _hdlr_func_union {
        /*
         * @brief The file descriptor's handler.
         * @note The first node is always the listening socket, and its handler is always
         * 			to accept a new connection and add it to the reactor.
        */
        handler_t handler;

        /*
         * @brief A pointer to the handler function.
         * @note This is a generic pointer, and it's used to print the handler's address.
        */
        void *handler_ptr;
    } hdlr;

    /*
     * @brief The next node in the linked list.
     * @note For the last node, this is NULL.
    */
    reactor_node_ptr next;
};

/*
 * @brief A reactor object - a linked list of file descriptors and their handlers.
 */
struct _reactor_t {
    /*
     * @brief The thread in which the reactor is running.
     * @note The thread is created in startReactor() and deleted in stopReactor().
     * 			The thread function is reactorRun().
    */
    pthread_t thread;

    /*
     * @brief The first node in the linked list.
     * @note The first node is always the listening socket.
    */
    reactor_node_ptr head;

    /*
     * @brief A pointer to an array of pollfd structures.
     * @note The array is allocated and freed in reactorRun().
     * @note The array is used in reactorRun() to call poll().
    */
    pollfd_t_ptr fds;

    /*
     * @brief A boolean value indicating whether the reactor is running.
     * @note The value is set to true in startReactor() and to false in stopReactor().
    */
    bool running;
};


/********************************/
/* Functions Declartion Section */
/********************************/

/*
 * @brief Create a reactor object - a linked list of file descriptors and their handlers.
 * @return A pointer to the created object, or NULL if failed.
 * @note The returned pointer must be freed.
 */
void *createReactor();

/*
 * @brief Start executing the reactor, in a new thread.
 * @param react A pointer to the reactor object.
 * @return void
 */
void startReactor(void *react);

/*
 * @brief Stop the reactor - delete the thread.
 * @param react A pointer to the reactor object.
 * @return void
 */
void stopReactor(void *react);

/*
 * @brief Add a file descriptor to the reactor.
 * @param react A pointer to the reactor object.
 * @param fd The file descriptor to add.
 * @param handler The handler function to call when the file descriptor is ready.
 * @return void
 */
void addFd(void *react, int fd, handler_t handler);

/*
 * @brief Wait for the reactor to finish.
 * @param react A pointer to the reactor object.
 * @return void
 */
void WaitFor(void *react);


/*
 * @brief A signal handler for SIGINT.
 * @note This function is called when the user presses CTRL+C.
 * 			It stops the reactor, closes all sockets and frees all memory,
 * 			Then, it exits the program.
 * @note This function is registered to SIGINT in main().
*/
void signal_handler();

/*
 * @brief A handler for a client socket.
 * @param fd The client socket file descriptor.
 * @param arg The reactor.
 * @return The reactor on success, NULL otherwise.
 * @note This function is called when a client sends a message to the server,
 * 			and prints the message.
*/
void *client_handler(int fd, void *react);

/*
 * @brief A handler for the server socket.
 * @param fd The server socket file descriptor.
 * @param arg The reactor.
 * @return The reactor on success, NULL otherwise.
 * @note This function is called when a new client connects to the server,
 * 			and adds the client to the reactor.
*/
void *server_handler(int fd, void *react);

#endif