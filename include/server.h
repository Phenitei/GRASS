#ifndef SERVER_H
#define SERVER_H

#include <grass.h>

/* Definitions */

// Configuration file
#define COMMENT_CHAR '#'
#define CONF_FILENAME "grass.conf"

// Parsing
// Maximum size of a word in config file
#define MAX_CONF_LINE_LEN 2048
// Maximum size of the user
#define MAX_USER_LEN 1024
// Maximum size of the pw
#define MAX_PW_LEN 1024
// Maximum size of the port
#define MAX_PORT_LEN 7
// Maximum size of absolute path
#define MAX_ABS_PATH_LEN 256
// Maximum size of path provided by user
#define MAX_PATH_LEN 128
// Maximum len of cmd name + flags
#define MAX_CMD_LEN 64
// Maximum len of argument provided by client
#define MAX_ARG_LEN 512
// Maximum len of response to client
#define MAX_RESPONSE_LEN 4096
// Maximum len of received message from client
#define MAX_RECV_LEN MAX_RESPONSE_LEN

#define MAX_BASE_PATH_LEN 128

#define OUTPUT_FILENAME ".output.temp"
// What we send the client if all went well
#define SUCCESS_RESPONSE "OK"
// The size of the array for commands
#define CMD_COUNT 15

/* Types */

// function to be called upon command call
typedef int (*cmd_handler)(char** argv);

typedef struct
{
    const char* name;       // command name
    const int argc;         // number of arguments
    const bool req_login;   // requires login

    cmd_handler handler;    // command handler
} cmd_type_t;

/* Enums */

// Parsing states
enum parse_state
{
    NEW_LINE = 1,
    COMMENT = 2,
    OPTION = 3,
    BASE = 4,
    PORT = 5,
    USER = 6,
    PASSW = 7
};

/* Functions */

void run_command(const char* command, int sock);

// Parsing
int next_word(FILE* src, char* dst, size_t len, char* until);
int parse_grass(void);

// initialization
int set_base_dir(const char* base_dir);
void free_structs(void);

// Response communication
void set_response(const char* msg);
void respond(int sockfd);

// Command execution
int exec_cmd(const char* cmd);
int parse_cmd(const char* str, cmd_type_t** ctype, char*** argv);

// Per-user thread
void* connection_thread(void* argvp);
int init_connection_thread(int sockfd);
void free_connection_thread(void);

// Transfer
void* transfer_thread(void* argvp);
int start_transfer_thread(const char* filename, FILE* f, size_t file_len, upon_transfer_start transferer);

// Command handlers
int handle_ls(char** argv);
int handle_ping(char** argv);
int handle_login(char** argv);
int handle_pass(char** argv);
int handle_cd(char** argv);
int handle_mkdir(char** argv);
int handle_rm(char** argv);
int handle_date(char** argv);
int handle_grep(char** argv);
int handle_whoami(char** argv);
int handle_w(char** argv);
int handle_logout(char** argv);
int handle_exit(char** argv);
int handle_get(char** argv);
int handle_put(char** argv);

// Helpers
bool contains_char(const char* cmd, size_t len, char c);
user_t* find_user(const char* name);
cmd_type_t* find_cmd_type(const char* cname);
void do_logout(void);
void print_userlist(void);
char* append_to_path(const char* abs_path, const char* rel_path);
char* rel_to_abs_path(const char* rel_path);
bool is_subpath_of(const char* path, const char* dir);
char* canonify_abs_path(const char* abs_path);
bool is_rel(const char* path);
bool abs_path_exists(const char* abs_path);
bool abs_path_is_dir(const char* abs_path);
bool path_too_long(const char* abs_path);

/* Static immutable globals */
static cmd_type_t cmd_types[CMD_COUNT] =
{
    {"ls", 0, true, handle_ls},
    {"ping", 1, false, handle_ping},
    {"login", 1, false, handle_login},
    {"pass", 1, false, handle_pass},
    {"cd", 1, true, handle_cd},
    {"mkdir", 1, true, handle_mkdir},
    {"rm", 1, true, handle_rm},
    {"date", 0, true, handle_date},
    {"grep", 1, true, handle_grep},
    {"whoami", 0, true, handle_whoami},
    {"w", 0, true, handle_w},
    {"logout", 0, true, handle_logout},
    {"exit", 0, false, handle_exit},
    {"get", 1, true, handle_get},
    {"put", 2, true, handle_put},
};

#endif /* ifndef CLIENT_H */
