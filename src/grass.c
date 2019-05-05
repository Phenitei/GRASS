#include <grass.h>
#include <stdio.h>

#define IP_PROTOCOL 0
#define SERVER_BACKLOG 3
#define LOCALHOST "127.0.0.1"

#define FLAG 0
#define CMD_DELIM ";"
#define MAX_LINE_SIZE 256

#define SAN_LIMIT 128

#define TR_FAIL_MSG "Error: file transfer failed.\n"
#define MAX_TR_SIZE 128


#define min(a, b) ((size_t)(int)((ssize_t)(a) < (b) ? (a) : (b)))

int
create_socket(void)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, IP_PROTOCOL);

    if(sockfd < 0)
    {
        perror("Error while creating socket");
        return sockfd;
    }

    return sockfd;
}


/**
 * Bind a socket to a port. If the passed port is zero, then we use the first
 * available port and set the pointer to that value. Otherwise, try to use the
 * given port.
 */
int
bind_server_socket(int sockfd, uint16_t* s_port)
{
    int error;
    struct sockaddr_in address;
    socklen_t len = sizeof(address);

    if((error = sock(&address, *s_port, LOCALHOST)))
        return error;

    if((error = bind(sockfd, (struct sockaddr*)&address, len))< 0)
    {
        perror("Error while binding socket to port");
        return error;
    }

    if ((error = getsockname(sockfd, (struct sockaddr *)&address, &len) != 0))
    {
        fprintf(stderr, "Error while retrieving port number\n");
        return error;
    }

    *s_port = ntohs(address.sin_port);

    if((error = listen(sockfd, SERVER_BACKLOG)) < 0)
    {
        perror("Error with socket while listening");
        return error;
    }

    return 0;
}

int
accept_connection(int sockfd, uint16_t s_port)
{
    int error;
    int new_socket;
    struct sockaddr_in address;
    size_t addrlen = sizeof(address);

    if((error = sock(&address, s_port, LOCALHOST)))
        return -1;

    if ((new_socket = accept(sockfd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
    {
        perror("Error while accept a new entering connection");
        return new_socket;
    }

    return new_socket;
}

int
connect_to(int sockfd, const char* addr, uint16_t d_port)
{
    int error;
    struct sockaddr_in address;

    if((error = sock(&address, d_port, addr)) < 0)
        return -1;

    if((error = connect(sockfd, (struct sockaddr*)&address, sizeof(address))) < 0)
    {
        perror("Error while connect");
        return error;
    }

    return 0;
}

int
sock(struct sockaddr_in* sock, uint16_t port, const char* addr)
{
    sock->sin_family = AF_INET;
    sock->sin_port = htons(port);

    if (inet_pton(AF_INET, addr, &sock->sin_addr) <= 0)
    {
        fprintf(stderr, "addr: %s is not valid\n", addr);
        return -1;
    }

    return 0;
}

int
send_cmd(int socket_fd, command_t *cmd)
{
    if(cmd == NULL || cmd->cname == NULL || cmd->cmd == NULL)
        return -1;

    size_t len = strlen(cmd->cname) + strlen(CMD_DELIM) + strlen(cmd->cmd) + 1;
    if(cmd->params)
    {
        len += strlen(CMD_DELIM) + strlen(cmd->params);
    }

    char *buffer = calloc(len, sizeof(char));
    strcpy(buffer, cmd->cname);
    strcat(buffer, CMD_DELIM);
    strcat(buffer, cmd->cmd);

    if(cmd->params)
    {
        strcat(buffer, CMD_DELIM);
        strcat(buffer, cmd->params);
    }

    send(socket_fd, buffer, len, FLAG);
    free(buffer);

    return 0;
}

int
recv_cmd(char* str, command_t *cmd)
{
    if(str == NULL || cmd == NULL)
        return -1;

    char *ptr = str;

    const char *tok_cname = strtok(ptr, CMD_DELIM);
    ptr = NULL;
    const char *tok_cmd = strtok(ptr, CMD_DELIM);
    ptr = NULL;

    if(tok_cname == NULL || tok_cmd == NULL)
        return -1;

    cmd->cname = tok_cname;
    cmd->cmd = tok_cmd;

    const char *tok_params = strtok(ptr, CMD_DELIM);

    if(tok_params)
        cmd->params = tok_params;

    return 0;
}

void
hijack_flow(void)
{
    printf("Method hijack: Accepted\n");
}

int
send_file(const char* filename, FILE* file, size_t file_len, int sockfd, FILE* outstream)
{
    (void)filename;

    char buffer[MAX_LINE_SIZE + 1];
    buffer[MAX_LINE_SIZE] = '\0';

    size_t act_file_len = 42;
    if (get_file_len(file, &act_file_len) < 0)
    {
        fprintf(outstream, "Error: file has no size...\n");
        return E_IO;
    }

    size_t sent_len = 0;
    size_t read_len = 0;
    char* ptr = NULL;

    while((ptr = fgets(buffer, MAX_LINE_SIZE, file)) != NULL)
    {
        read_len = strlen(buffer);
        send(sockfd, buffer, read_len, 0);
        sent_len += read_len;

        if (sent_len > file_len)
            break;

    }

    if (sent_len != file_len)
    {
        printf(TR_FAIL_MSG);
        fprintf(outstream, "Error: transfer failed error: sent %lu/%lu bytes\n", sent_len, file_len);
    }

    return 0;
}

int
recv_file(const char* filename, FILE* file, size_t file_len, int sockfd, FILE* outstream)
{

    char buffer[MAX_TR_SIZE + 1];
    buffer[MAX_TR_SIZE] = '\0';
    size_t to_recv_len;
    size_t written_len = 0;

    while(written_len < file_len)
    {
        to_recv_len = min(file_len - written_len, MAX_TR_SIZE);
        ssize_t recv_len = recv(sockfd, buffer, to_recv_len, 0);
        if (recv_len >= 0)
            buffer[recv_len] = '\0';

        fprintf(file, buffer);
        written_len += (size_t)recv_len;

        if (recv_len <= 0 || written_len > file_len)
        {
            if (recv_len < 0)
                perror("recv_len was -1");

            break;
        }
    }

    if (written_len != file_len)
    {
        fprintf(outstream, TR_FAIL_MSG);
        remove(filename);
        return E_TR;
    }

    return 0;
}

int
get_file_len(FILE* f, size_t* len)
{
    ssize_t ret;
    fseek(f, 0, SEEK_END);

    if((ret = ftell(f)) < 0)
        return -1;

    *len = (size_t)ret;
    fseek(f, 0, SEEK_SET);
    return 0;
}

int
sanitize(char* san_str, const char* str)
{
    char escaped_chars[18] = {'\\', '"', '`', '$', '(', '{', '[', ')', '}', ']', ';', '&', '|', '~', '?', '!', '<', '>'};

    size_t orig_len = strlen(str);
    const char *const str_end = str + orig_len;

    const char* orig_ptr = str;
    char* san_ptr = san_str;

    while(orig_ptr <= str_end)   // includes \0
    {
        char c = *orig_ptr;
        size_t i = 0;

        for (i = 0; i < sizeof(escaped_chars) && c != escaped_chars[i]; ++i);

        if (i < sizeof(escaped_chars))
            *(san_ptr++) = '\\';

        *(san_ptr++) = *(orig_ptr++);
    }

    return 0;
}
