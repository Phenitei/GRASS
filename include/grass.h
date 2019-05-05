#ifndef GRASS_H
#define GRASS_H

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct
{
    const char* uname;
    const char* pass;

    bool isLoggedIn;
} user_t;

typedef struct
{
    const char* cname;
    const char* cmd;
    const char* params;
} command_t;

enum error_code
{
    E_NOTFOUND = -666,
    E_IO,
    E_NOMEM,
    E_PROTO,
    E_PARSE,
    E_TOOLARGE,
    E_NULLPTR,
    E_DIR,
    E_AUTH,
    E_PERM,
    E_TR
};

/**
 * Create a TCP/IPV4 socket
 *
 * @return the socket descriptor (like a file-handle), error if <0 is returned
 */
int create_socket(void);

/**
 * Bind and listen on a the given port
 *
 * @param sockfd socket descriptor (must be initialized before)
 * @param s_port source port to listen on. If zero, will use next available port.
 * @return <0 if error, 0 if everything is fine.
 */
int bind_server_socket(int sockfd, uint16_t* s_port);

/**
 * Accept a entering connection
 *
 * @param sockfd socket descriptor (must be initialized and binded before used)
 * @param s_port source port to listen on
 * @return <0 if error, the new socket id if everything went fine
 */
int accept_connection(int sockfd, uint16_t s_port);

/**
 * Connect on given addr and port
 *
 * @param sockfd socket descriptor (must be initialized before)
 * @param addr IPv4 address to connect to
 * @param d_port source port to listen on
 * @return <0 if error, 0 if everything is fine.
 */
int connect_to(int sockfd, const char* addr, uint16_t d_port);

/**
 * Method used in grass.c to return an ipv4 sockaddr with the given port
 *
 * @param sockaddr_in pointer to socket which write data
 * @param port port of the socket to have
 * @param addr address to configure (on which to listen or to connect)
 * @return 0 if no error, -1 if errors
 */
int sock(struct sockaddr_in* sock, uint16_t port, const char* addr);

/*
 * Send command throught the socket. The command will be converted to a
 * string before being sended;
 *
 * @param socket_fd file descriptor of the socket. Must be initialized and
 * connected to server before used
 * @param cmd command to send
 * @return <0 if error, 0 if everything is fine.
 */
int send_cmd(int socket_fd, command_t *cmd);

/*
 * Translate string received through a socket to a command. It translate the
 * string with the reverse method used in send_cmd and fill the struct.
 *
 * @param str The string containing the data
 * @param cmd the struct to fill, must be allocated before the
 * function call.
 * @return <0 if error, 0 if everything is fine
 */
int recv_cmd(char* str, command_t *cmd);

/*
 * Function to call to prove that the control-flow has been hijacked.
 */
void hijack_flow(void);

typedef int (*upon_transfer_start)(const char* filename, FILE* f, size_t f_size, int sockfd, FILE* outstream);

int send_file(const char* filename, FILE* f, size_t file_len, int sockfd, FILE* outstream);
int recv_file(const char* filename, FILE* f, size_t file_len, int sockfd, FILE* outstream);
int get_file_len(FILE* f, size_t* size);

typedef struct
{
    FILE* file;
    size_t file_len;
    const char* filename;
    int sockfd;
    uint16_t port;
    upon_transfer_start transfer;
} transfer_t;

int sanitize(char* san_str, const char* str);

#endif /* ifndef GRASS_H */
