#ifndef CLIENT_H
#define CLIENT_H

#include <grass.h>

/** Number of implemented commands */
#define NUM_COMMANDS        15
/** Maximum length of user input */
#define MAX_CHAR_LEN        4096
/** Return value for exiting */
#define BYE_BYE             1
/** Fatal errer, kill the REPL */
#define ERR_KILLED          2
/** Invalid command error */
#define ERR_SH_ICMD         127
/** Invalid command error message */
#define ERR_SH_ICMD_STR     "invalid command"
/** Invalid number of arguments to commands */
#define ERR_SH_WARGC        3
/** Invalid number of arguments message */
#define ERR_SH_WARGC_STR    "wrong number of arguments"
/** Delimiter between command and arguments */
#define CMD_DELIM           " "
/** Memory allocation error */
#define ERR_NO_MEM          4

/* Because it's only defined in POSIX and not C11 */
#ifndef __ssize_t_defined
typedef long signed int ssize_t; /**< signed long integers */
#define __ssize_t_defined
#endif /* ifndef __ssize_t_defined */

/** Pointers to REPL commands */
typedef int (*shell_fct)(size_t, char**);

/**
 * Mapping from a command, to it's implementation details
 */
struct shell_map
{
    const char* name;   /**< Command name */
    shell_fct fct;      /**< Function implementing the command */
    const char* help;   /**< Help text */
    size_t argc;        /**< Number of arguments */
    const char* args;   /**< Argument description */
};

/* Main functions: headless and REPL */
int repl(void);

/* Shell utilities */
int     err_deal(int);
void    free_tokenized(char**, size_t);
int     run_cmd(char**, size_t);
ssize_t tokenize_input(char*, char*** const);

/* Shell command prototypes */
int do_simple(size_t, char**);
int do_get(size_t, char**);
int do_put(size_t, char**);
int do_exit(size_t, char**);

/* get/put commands */
int handle_get(uint16_t port);
int handle_put(uint16_t port);
int start_transfer_thread(FILE* f, uint16_t port, upon_transfer_start transferer);
void* transfer_thread(void* argvp);


/** Array containing all implemented commands */
struct shell_map shell_cmds[NUM_COMMANDS] =
{
    {   "login",  do_simple,  "login as a user",  1, "username to login with" }
    , { "pass",   do_simple,  "user password",    1, "password"               }
    , { "ping",   do_simple,  "execute a ping",   1, "host to ping"           }
    , { "ls",     do_simple,  "list files",       0, ""                       }
    , { "cd",     do_simple,  "change directory", 1, "target directory"       }
    , { "mkdir",  do_simple,  "create directory", 1, "target directory"       }
    , { "rm",     do_simple,  "remove entry",     1, "target file/directory"  }
    , { "get",    do_get,     "get a file",       1, "file to get"            }
    , { "put",    do_put,     "put a file",       2, "filename and filesize"  }
    , { "grep",   do_simple,  "grep a pattern",   1, "the pattern to search"  }
    , { "date",   do_simple,  "query date",       0, ""                       }
    , { "whoami", do_simple,  "get current user", 0, ""                       }
    , { "w",      do_simple,  "get logged users", 0, ""                       }
    , { "logout", do_simple,  "log out",          0, ""                       }
    , { "exit",   do_exit,    "exit the REPL",    0, ""                       }
};

#endif /* ifndef CLIENT_H */
