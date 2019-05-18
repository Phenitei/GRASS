#include <server.h>
#include <ctype.h>

/* Static mutable globals */

static __thread user_t* logged_user = NULL;
static __thread bool logging = false;

static user_t **userlist = NULL;
static int num_users = 0;
static char* base_abs_path = NULL;
static __thread char* curr_abs_path = NULL;
static __thread char* output_abs_path = NULL;
static char* port = NULL;

static __thread char* response_buff = NULL;

// Conf file parsing
/**
   Reads characters from provided file pointer into dst, either until len
   characters are read, or until a space, newline or EOF is reached.

   @param src Source file pointer
   @param dst char* where read characters will be moved
   @param len Maximum number of characters that are allowed to fit in dst
   @param until Will contain the character that broke the word. Can be space,
   newline, or EOF

   @return 0 on success, EOF if end of file was reached, negative value otherwise
 */
int
next_word(FILE* src, char* dst, size_t len, char* until)
{
    int ci;
    char c;
    size_t idx = 0;

    if (src == NULL || dst == NULL)
    {
        fprintf(stderr, "Null pointer in next_word\n");
        return EINVAL;
    }

    if (len < 2)
    {
        fprintf(stderr, "len too small\n");
        return EINVAL;
    }

    while ((ci = getc(src)) != EOF)
    {
        c = (char) ci;

        if (c == ' ' || c == '\n')
        {
            *until = c;
            dst[idx] = '\0';
            return (int)idx;
        }
        else if (idx >= len - 1)
        {
            dst[0] = '\0';
            *until = '\0';
            fprintf(stderr, "Too long word in config file");
            return -1;
        }

        dst[idx++] = c;
    }

    *until = '\n';
    dst[idx] = '\0';

    if (idx == 0)
        return EOF;

    return (int)idx;
}

/**
   Parse the grass.conf file and fill in the global variables

   @return
   0 on success,
   EPROTO on parse error,
   ENOENT on conf file not found,
   ENOMEM otherwise
*/
int
parse_grass(void)
{
    int ret = 0;
    FILE* f = fopen(CONF_FILENAME, "r");

    if (f == NULL)
    {
        fprintf(stderr, "Cannot find configuration file grass.conf");
        return E_NOTFOUND;
    }

    // malloc static structures
    num_users = 0;
    userlist = malloc(sizeof(user_t*));
    port = malloc(sizeof(char) * MAX_PORT_LEN);
    if (userlist == NULL || port == NULL)
    {
        fclose(f);
        return E_NOMEM;
    }

    // Parse loop
    char word[MAX_CONF_LINE_LEN + 1];
    word[MAX_CONF_LINE_LEN] = '\0';

    if (word == NULL)
    {
        fclose(f);
        return E_NOMEM;
    }

    word[MAX_CONF_LINE_LEN - 1] = '\0';

    int state = NEW_LINE;
    char next;

    while (true)
    {
        ret = next_word(f, word, MAX_CONF_LINE_LEN, &next);
        if (ret < 0)
            return ret;

        else if (ret == 0)
            continue;

        switch (state)
        {
        case NEW_LINE:
            if (word[0] == COMMENT_CHAR)
                state = COMMENT;

            else if (word[0] != '\n')
            {
                if (next == '\n')
                {
                    fprintf(stderr, "Unexpected newline\n");
                    return E_PARSE;
                }
                else if (next == ' ')
                {
                    if (strncmp(word, "base", MAX_CONF_LINE_LEN) == 0)
                        state = BASE;

                    else if (strncmp(word, "port", MAX_CONF_LINE_LEN) == 0)
                        state = PORT;

                    else if (strncmp(word, "user", MAX_CONF_LINE_LEN) == 0)
                        state = USER;

                    else
                    {
                        printf("Unknown option %s\n", word);
                        return E_PARSE;
                    }
                }
            }
            break;

        case COMMENT :
            if (next == '\n')
                state = NEW_LINE;

            break;

        case BASE:
            if (next == '\n')
            {
                state = NEW_LINE;
                if ((ret = set_base_dir(word)) < 0)
                {
                    fprintf(stderr, "Impossible to set base dir");
                    return ret;
                }
            }
            else
                return E_PARSE;

            break;

        case PORT:
            if (next == '\n')
            {
                state = NEW_LINE;
                strncpy(port, word, MAX_PORT_LEN);
            }
            else
                return E_PARSE;

            break;
        case USER:
            if (next == ' ')
            {
                state = PASSW;

                num_users++;
                user_t* user = malloc(sizeof(user_t));
                char* username = malloc(sizeof(char) * MAX_USER_LEN);

                strncpy(username, word, MAX_USER_LEN);
                userlist = realloc(userlist, sizeof(user_t*) * (unsigned int)num_users);

                if (user == NULL || username == NULL || userlist == NULL)
                {
                    fclose(f);
                    return E_NOMEM;
                }

                user->uname = username;
                user->isLoggedIn = false;
                userlist[num_users-1] = user;
            }
            else
                return E_PARSE;

            break;

        case PASSW:
            if (next == '\n')
            {
                state = NEW_LINE;

                char* password = malloc(sizeof(char) * MAX_PW_LEN);
                strncpy(password, word, MAX_USER_LEN);
                if (password == NULL)
                {
                    fclose(f);
                    return E_NOMEM;
                }

                userlist[num_users-1]->pass = password;
            }
            else
                return E_PARSE;

            break;

        default:
            printf("(%d?)", state);
        }
    }

    fclose(f);
    return 0;
}



// Per-user thread
int
init_connection_thread(int sockfd)
{
    // Init curr path
    curr_abs_path = malloc(sizeof(char) * (MAX_ABS_PATH_LEN + 1));
    curr_abs_path[MAX_ABS_PATH_LEN] = '\0';
    strncpy(curr_abs_path, base_abs_path, MAX_ABS_PATH_LEN);

    // Init output path
    char intermediate_path[MAX_ABS_PATH_LEN + 1];
    intermediate_path[MAX_ABS_PATH_LEN] = '\0';
    sprintf(intermediate_path, "%s/../%s%d", base_abs_path, OUTPUT_FILENAME, sockfd);
    output_abs_path = canonify_abs_path(intermediate_path);

    // Init response buff
    response_buff = malloc(sizeof(char) + (MAX_RESPONSE_LEN + 1));
    if (response_buff == NULL)
        return E_NOMEM;

    response_buff[MAX_RESPONSE_LEN] = '\0';

    return 0;
}

void*
connection_thread(void* argvp)
{
    int ret;
    ssize_t recv_len;

    int sockfd = ((int*)argvp)[0];
    free(argvp);

    if ((ret = init_connection_thread(sockfd)) < 0)
        return NULL;

    char buff[MAX_RECV_LEN + 1];
    buff[MAX_RECV_LEN] = '\0';

    while((recv_len = recv(sockfd, buff, MAX_RECV_LEN, 0)) > 0)
    {
        buff[recv_len] = '\0';
        run_command(buff, sockfd);
    }

    do_logout();

    close(sockfd);
    free_connection_thread();

    return NULL;
}

void
free_connection_thread()
{
    free(curr_abs_path);
    curr_abs_path = NULL;
    free(output_abs_path);
    output_abs_path = NULL;

    free(response_buff);
    response_buff = NULL;

    logged_user = NULL;
    logging = false;
}


// Transfer
int
start_transfer_thread(const char* filename, FILE* f, size_t file_len, upon_transfer_start transferer)
{
    int ret;

    int fd;
    if ((fd = create_socket()) < 0)
    {
        fprintf(stderr, "Error while creating socket\n");
        return fd;
    }

    uint16_t tr_port = 0;
    if ((ret = bind_server_socket(fd, &tr_port)) < 0)
    {
        fprintf(stderr, "Error while binding to free port\n");
        return ret;
    }

    transfer_t* tr = malloc(sizeof(transfer_t));
    if (tr == NULL)
    {
        fprintf(stderr, "Error nomem\n");
        return E_NOMEM;
    }

    tr->sockfd = fd;
    tr->file = f;
    tr->filename = filename;
    tr->file_len = file_len;
    tr->port = tr_port;
    tr->transfer = transferer;

    pthread_t th;
    pthread_create(&th, NULL, transfer_thread, (void*)tr);

    return (int)tr_port;
}

void*
transfer_thread(void* argvp)
{
    transfer_t* tr = (transfer_t*)argvp;

    int fd = tr->sockfd;
    FILE* f = tr->file;
    size_t f_len = tr->file_len;
    uint16_t tr_port = tr->port;

    int tr_fd;
    if ((tr_fd = accept_connection(fd, tr_port)) > 0)
    {
        tr->transfer(tr->filename, f, f_len, tr_fd, stdout);
    }

    close(tr_fd);
    close(fd);
    fclose(f);

    return NULL;
}

// Initialization
int
set_base_dir(const char* b_dir)
{
    if (b_dir == NULL)
        return E_NULLPTR;

    char cwd[MAX_ABS_PATH_LEN + 1];
    cwd[MAX_ABS_PATH_LEN] = '\0';
    if (getcwd(cwd, MAX_ABS_PATH_LEN) == NULL)
    {
        fprintf(stderr, "Could not get cwd\n");
        return E_DIR;
    }

    if (is_rel(b_dir))
    {
        char* abs_cwd = canonify_abs_path(cwd);
        base_abs_path = append_to_path(abs_cwd, b_dir);
        free(abs_cwd);
    }
    else
        base_abs_path = canonify_abs_path(b_dir);

    if (base_abs_path == NULL)
        return E_DIR;

    return 0;
}

void
free_structs()
{
    for (int i = 0; i < num_users; ++i)
    {
        free(userlist[i]);
        userlist[i] = NULL;
    }

    free(userlist);
    userlist = NULL;

    free(base_abs_path);
    base_abs_path = NULL;
    free(port);
    port = NULL;
}

// Response communication
void
set_response(const char* msg)
{
    strncpy(response_buff, msg, MAX_RESPONSE_LEN);
}

void
respond(int sockfd)
{
    char msg[strlen(response_buff) + strlen(SUCCESS_RESPONSE) + 1 + 1];

    sprintf(msg, "%s", response_buff);
    if (strlen(msg) == 0)
      sprintf(msg, "OK");

    if(strncmp(response_buff, "OK", MAX_RESPONSE_LEN) != 0
            && strncmp(response_buff, "get port", 8) != 0
            && strncmp(response_buff, "put port", 8) != 0)
        fprintf(stdout, "%s\n", response_buff);

    send(sockfd, msg, strlen(msg), 0);

    set_response(SUCCESS_RESPONSE);
}

// Command execution
void
run_command(const char* command, int sock)
{
    int ret;

    set_response(SUCCESS_RESPONSE);

    cmd_type_t* ctype;
    char** argv;

    ret = parse_cmd(command, &ctype, &argv);
    if (ret != 0)
    {
        respond(sock);
        return;
    }

    if (ctype->handler(argv) != 0)
    {
        respond(sock);
        return;
    }

    if (ctype->req_login && !(logged_user != NULL && logged_user->isLoggedIn))
    {
        set_response("Error: This command requires authentication");
        respond(sock);
        return;
    }

    for (int i = 0; i < ctype->argc; ++i)
    {
        free(argv[i]);
        argv[i] = NULL;
    }
    free(argv);
    argv = NULL;

    respond(sock);
}

int
parse_cmd(const char* str, cmd_type_t** ctype, char*** pargv)
{
    if (str == NULL || ctype == NULL || pargv == NULL)
        return E_NULLPTR;

    set_response("Error: Internal error");

    // create mutable string
    size_t len_str = strlen(str);
    char mut_str[len_str + 1];
    mut_str[len_str] = '\0';
    strncpy(mut_str, str, len_str);

    // extract command type
    char* cname = strtok(mut_str, " ");
    *ctype = find_cmd_type(cname);
    if (*ctype == NULL)
    {
        set_response("Error: Command not found");
        return E_NOTFOUND;
    }

    if (strcmp(cname, "pass") != 0 && logging)
    {
        logging = false;
        logged_user = NULL;
        set_response("Error: Expected 'pass' command after login. Aborting login");
        return E_PROTO;
    }

    // extract args
    int argc = 0;
    *pargv = malloc(sizeof(char*) * (size_t)argc);

    char* arg_ptr = strtok(NULL, "\0");
    char* read_ptr = arg_ptr;
    char* write_ptr = arg_ptr;
    char quote = '\0';
    bool in_whitespace = false;
    char c;

    while (arg_ptr != NULL)
    {
        c = *read_ptr;
        // for every quote
        if (c == '`' || c == '\'' || c == '"')
        {
            // if it was escaped
            if (read_ptr > arg_ptr && *(read_ptr - 1) == '\\')
            {
                *(write_ptr - 1) = c;
                read_ptr++;
                continue;
            }
            // update current enclosing quote
            if (c == quote)
            {
                quote = '\0';
                read_ptr++;
                continue;
            }
            else if (quote == '\0')
            {
                quote = c;
                read_ptr++;
                continue;
            }
        }

        if ((c == ' ' || c == '\0') && quote == '\0' && !in_whitespace)
        {
            in_whitespace = true;
            *(write_ptr) = '\0';
            write_ptr++;
            read_ptr++;

            // adding arg to argv
            argc++;
            *pargv = realloc(*pargv, sizeof(char*) * (size_t)argc);

            if (*pargv == NULL)
                return E_NOMEM;

            size_t arg_len = strlen(arg_ptr);
            if (arg_len > MAX_ARG_LEN)
                arg_len = MAX_ARG_LEN;

            char* arg_copy = malloc(sizeof(char) * (arg_len + 1) );
            if (arg_copy == NULL)
            {
                free(*pargv);
                return E_NOMEM;
            }

            arg_copy[arg_len] = '\0';
            strncpy(arg_copy, arg_ptr, arg_len);
            (*pargv)[argc - 1] = arg_copy;

            // arg ends
            arg_ptr = write_ptr;
        }
        else if (c == ' ' && in_whitespace)
        {
            // ignore
            read_ptr++;
            continue;
        }
        else
        {
            if (c != ' ' && in_whitespace)
            {
                in_whitespace = false;
                // new arg starts
            }
            *(write_ptr++) = *(read_ptr++);
        }

        if (c == '\0')
            break;
    }

    if (quote != '\0')
    {
        set_response("Error: Missing end quote");
        return E_PARSE;
    }

    // checking number of args
    if (argc != (*ctype)->argc)
    {
        for (int i = 0; i < argc; ++i)
            free((*pargv)[i]);

        free(*pargv);
        set_response("Error: Wrong number of arguments.");
        return E_PROTO;
    }

    set_response(SUCCESS_RESPONSE);

    return 0;
}

int
exec_cmd(const char* cmd)
{
    set_response("Error: Internal error");

    if (cmd == NULL)
        return E_NULLPTR;

    int ret;
    size_t response_len = MAX_RESPONSE_LEN;
    char* response = malloc(sizeof(char) * (response_len + 1));

    if (response == NULL)
        return ENOMEM;

    response[response_len] = '\0';

    size_t filename_len = strlen(output_abs_path);
    size_t cmd_len = strlen(cmd);

    char* piped_cmd = malloc(sizeof(char) * (cmd_len + filename_len + 20 + 1));
    if (piped_cmd == NULL)
    {
        set_response("Error: Internal error");
        return E_NOMEM;
    }

    sprintf(piped_cmd, "(%s) > %s 2>&1", cmd, output_abs_path);

    if ((ret = system(piped_cmd)) < 0)
    {
        set_response("Error: Internal error upon command execution on host system");
        return ret;
    }

    free(piped_cmd);
    piped_cmd = NULL;

    // reading output
    FILE* f = fopen(output_abs_path, "r");
    if (f == NULL)
    {
        set_response("Error: Hmmm, you're doing some weird stuff...");
        return E_IO;
    }

    int c;
    size_t counter = 0;

    while ((c = fgetc(f)) != EOF && counter < response_len)
        response[counter++] = (char)c;

    response[counter] = '\0';
    set_response(response);

    free(response);
    response = NULL;

    fclose(f);
    remove(output_abs_path);

    return 0;
}

int
handle_ls(char** argv)
{
    (void) argv;
    int cmd_len = (MAX_CMD_LEN + MAX_ABS_PATH_LEN);
    char cmd[cmd_len + 1];

    cmd[cmd_len] = '\0';
    sprintf(cmd, "ls -l \"%s\"", curr_abs_path);

    return exec_cmd(cmd);
}

int
handle_ping(char** argv)
{
    char san_arg[MAX_ARG_LEN + 1];
    san_arg[MAX_ARG_LEN] = '\0';
    sanitize(san_arg, argv[0]);

    size_t cmd_len = MAX_CMD_LEN + MAX_ARG_LEN + 1;
    char* cmd = malloc(sizeof(char) * (cmd_len + 1));
    cmd[cmd_len] = '\0';
    sprintf(cmd, "ping \"%s\" -c 1", san_arg);

    int ret = exec_cmd(cmd);

    free(cmd);
    return ret;
}

int
handle_login(char** argv)
{
    if (logged_user != NULL)
    {
        logged_user->isLoggedIn = false;
        logged_user = NULL;
    }

    char* username = argv[0];

    user_t* user = find_user(username);
    if (user == NULL)
    {
        set_response("Error: Username not found. Aborting.");
        return E_NOTFOUND;
    }

    logged_user = user;
    logging = true;

    return 0;
}

int
handle_pass(char** argv)
{
    if (!logging || logged_user == NULL)
    {
        set_response("Error : pass must be called directly after login");
        return E_PROTO;
    }

    logging = false;

    if (logged_user->isLoggedIn)
    {
        set_response("Error: This user is already connected somewhere else");
        logged_user = NULL;
        return E_PROTO;
    }

    if (strcmp(argv[0], logged_user->pass) == 0)
    {
        logged_user->isLoggedIn = true;
    }
    else
    {
        logged_user = NULL;
        set_response("Error : Authentication failed.");
        return E_AUTH;
    }

    return 0;
}

int
handle_cd(char** argv)
{
    char* new_abs_path = append_to_path(curr_abs_path, argv[0]);
    if (new_abs_path == NULL)
    {
        set_response("Error : access denied!");
        return E_DIR;
    }

    if (!is_subpath_of(new_abs_path, base_abs_path))
    {
        set_response("Error : access denied!");
        return E_PERM;
    }

    if (path_too_long(new_abs_path))
    {
        set_response("Error : the path is too long.");
        return E_DIR;
    }

    if (abs_path_is_dir(new_abs_path))
    {
        free(curr_abs_path);
        curr_abs_path = new_abs_path;
    }
    else
    {
        set_response("Error : directory not found");
        return E_NOTFOUND;
    }

    return 0;
}

int
handle_mkdir(char** argv)
{
    int ret;

    size_t arg_len = strlen(argv[0]);
    if (contains_char(argv[0], arg_len, '/') || contains_char(argv[0], arg_len, '~'))
    {
        set_response("Error : Please specify file or directory name within current directory");
        return E_PROTO;
    }

    char* abs_path = append_to_path(curr_abs_path, argv[0]);
    if (abs_path == NULL)
    {
        set_response("Error : path creation failed");
        return E_DIR;
    }

    if (path_too_long(abs_path))
    {
        set_response("Error : the path is toolong.");
        return E_DIR;
    }

    if (!is_subpath_of(abs_path, base_abs_path))
    {
        set_response("Error : Access denied!");
        return E_PERM;
    }

    if (abs_path_is_dir(abs_path))
    {
        set_response("Error : Directory already exists");
        return 0;
    }

    if ((ret = mkdir(abs_path, S_IRUSR | S_IWUSR | S_IXUSR)) < 0)
    {
        set_response("Error : Impossible to create directory");
        return E_IO;
    }

    free(abs_path);

    return 0;
}

int
handle_rm(char** argv)
{
    size_t arg_len = strlen(argv[0]);
    if (contains_char(argv[0], arg_len, '/') || contains_char(argv[0], arg_len, '~'))
    {
        set_response("Error : Please specify file or directory name within current directory");
        return E_PROTO;
    }

    char* abs_path = append_to_path(curr_abs_path, argv[0]);
    if (abs_path == NULL)
    {
        set_response("Error : path creation failed");
        return E_DIR;
    }

    if (path_too_long(abs_path))
    {
        set_response("Error : the path is toolong.");
        return E_DIR;
    }

    if (!is_subpath_of(abs_path, base_abs_path))
    {
        set_response("Error : Access denied!");
        return E_PERM;
    }

    int ret;

    if ((ret = remove(abs_path)) != 0)
    {
        set_response("Error : Could not execute remove");
        return E_IO;
    }

    free(abs_path);
    abs_path = NULL;

    return 0;
}

int
handle_date(char** argv)
{
    (void)argv;
    return exec_cmd("date");
}

int
handle_grep(char** argv)
{
    size_t cmd_len = (MAX_CMD_LEN + MAX_ARG_LEN + MAX_ABS_PATH_LEN * 2);
    char* cmd = malloc(sizeof(char) + (cmd_len + 1));
    cmd[cmd_len] = '\0';

    char san_arg[MAX_ARG_LEN + 1];
    san_arg[MAX_ARG_LEN] = '\0';
    sanitize(san_arg, argv[0]);
    sprintf(cmd, "cd \"%s\";grep -rl \"%s\"; cd \"%s\"", curr_abs_path, san_arg, base_abs_path);

    int ret = exec_cmd(cmd);
    free(cmd);
    return ret;
}

int
handle_whoami(char** argv)
{
    (void)argv;

    if (logged_user == NULL)
    {
        set_response("Error : No logged user");
        return E_PERM;
    }

    set_response(logged_user->uname);

    return 0;
}

int
handle_w(char** argv)
{
    (void)argv;

    char response[MAX_RESPONSE_LEN + 1];
    response[MAX_RESPONSE_LEN] = '\0';
    int len = 0;

    for(int i = 0; i < num_users; ++i)
    {
        user_t* u = userlist[i];
        if (u->isLoggedIn)
        {
            int written = 0;
            sprintf(response + len, "%s %n", u->uname, &written);
            len += written;
        }
    }

    set_response(response);

    return 0;
}

int
handle_logout(char** argv)
{
    (void)argv;

    do_logout();

    return 0;
}

int
handle_exit(char** argv)
{
    (void)argv;

    do_logout();

    return 0;
}

int
handle_get(char** argv)
{
    int ret;
    size_t arg_len = strlen(argv[0]);
    if(contains_char(argv[0], arg_len, '/') || contains_char(argv[0], arg_len, '~'))
    {
        set_response("Error: Please specify file name within current directory");
        return E_PROTO;
    }

    char* abs_path = append_to_path(curr_abs_path, argv[0]);
    if (abs_path == NULL)
    {
        set_response("Error: path creation failed");
        return E_DIR;
    }

    if (path_too_long(abs_path))
    {
        set_response("Error : the path is toolong.");
        return E_DIR;
    }

    if (!is_subpath_of(abs_path, base_abs_path))
    {
        set_response("Error: Access denied!");
        free(abs_path);
        return E_PERM;
    }

    FILE* f = fopen(abs_path, "r");
    free(abs_path);
    if (f == NULL)
    {
        set_response("Error: No such file");
        return E_NOTFOUND;
    }

    size_t file_len;
    if ((ret = get_file_len(f, &file_len)) < 0)
    {
        set_response("Error: Could not retrieve file size");
        return ret;
    }

    char* response = malloc(sizeof(char) * MAX_RESPONSE_LEN);
    if (response == NULL)
    {
        set_response("Error: nomem");
        return E_NOMEM;
    }

    int tr_port = start_transfer_thread(abs_path, f, file_len, send_file);
    if (tr_port < 0)
    {
        set_response("Error: Could not setup transfer thread");
        return tr_port;
    }

    sprintf(response, "get port: %d size: %lu", tr_port, file_len);
    set_response(response);

    free(response);

    return 0;
}

int
handle_put(char** argv)
{
    size_t arg_len = strlen(argv[0]);
    if(contains_char(argv[0], arg_len, '/') || contains_char(argv[0], arg_len, '~'))
    {
        set_response("Error: Please specify file name within executable's directory");
        return E_PROTO;
    }

    char* abs_path = append_to_path(curr_abs_path, argv[0]);
    if (abs_path == NULL)
    {
        set_response("Error: Path creation failed");
        return E_DIR;
    }

    if (path_too_long(abs_path))
    {
        set_response("Error : the path is toolong.");
        return E_DIR;
    }

    if (!is_subpath_of(abs_path, base_abs_path))
    {
        set_response("Error: Access denied!");
        free(abs_path);
        return E_PERM;
    }

    FILE* f = fopen(abs_path, "w");
    free(abs_path);
    if (f == NULL)
    {
        set_response("Error: Impossible to open file");
        return E_IO;
    }

    char* end_ptr;
    size_t file_len = strtoul(argv[1], &end_ptr, 10);

    char* response = malloc(sizeof(char) * MAX_RESPONSE_LEN);
    if (response == NULL)
    {
        set_response("Error: nomem");
        return E_NOMEM;
    }

    int tr_port = start_transfer_thread(abs_path, f, file_len, recv_file);
    if (tr_port < 0)
    {
        set_response("Error: Could not setup transfer thread");
        return tr_port;
    }

    sprintf(response, "put port: %d", tr_port);
    set_response(response);
    free(response);

    return 0;
}

// Helpers
bool
contains_char(const char* str, size_t len, char c)
{
    for (size_t i = 0; i < len; ++i)
    {
        if (str[i] == c)
            return true;
    }
    return false;
}

user_t*
find_user(const char* name)
{
    if (name == NULL)
        return NULL;

    for (int i = 0; i < num_users; ++i)
    {
        const char* cmp = userlist[i]->uname;
        if (strcmp(name, cmp) == 0)
            return userlist[i];

    }
    return NULL;
}

cmd_type_t*
find_cmd_type(const char* cname)
{
    if (cname == NULL)
        return NULL;


    for (int i = 0; i < CMD_COUNT; ++i)
    {
        cmd_type_t ctype = cmd_types[i];

        if (strcmp(cname, ctype.name) == 0)
            return cmd_types + i;

    }

    return NULL;
}

void
do_logout(void)
{
    if (logged_user != NULL)
    {
        logged_user->isLoggedIn = false;
        logged_user = NULL;
        logging = false;
    }
}

bool
is_subpath_of(const char* abs_path, const char* abs_dir)
{
    return strncmp(abs_dir, abs_path, strlen(abs_dir)) == 0;
}

bool
is_rel(const char* path)
{
    return (path != NULL && strlen(path) > 0 && path[0] != '/' && path[0] != '~');
}

char*
append_to_path(const char* abs_path, const char* rel_path)
{
    if (!is_rel(rel_path))
        return NULL;

    char concat_path[MAX_ABS_PATH_LEN + 1];
    sprintf(concat_path, "%s/%s", abs_path, rel_path);
    char* new_path = canonify_abs_path(concat_path);

    return new_path;
}

bool
abs_path_exists(const char* abs_path)
{
    struct stat st;
    return (stat(abs_path, &st) == 0);
}

bool
abs_path_is_dir(const char* abs_path)
{
    struct stat st;
    return (stat(abs_path, &st) == 0 && (S_ISDIR(st.st_mode)));
}

bool
path_too_long(const char* abs_path)
{
    return (strlen(abs_path) > strlen(base_abs_path) + MAX_BASE_PATH_LEN);
}

char*
canonify_abs_path(const char* abs_path)
{
    char* can_path = malloc(sizeof(char) * MAX_ABS_PATH_LEN);

    const char* s = abs_path;
    char* d = can_path;

    int s_len = (int)strlen(s);

    if (s_len == 0 || s[0] != '/')
        return NULL;

    int s_idx = 0;
    int d_idx = 0;

    char c;

    while (s_idx <= s_len && d_idx < MAX_ABS_PATH_LEN)   // == to include the \0
    {
        c = s[s_idx];
        d[d_idx] = c;
        if (c == '/' || c == '\0')
        {
            if (s_idx > 0 && s[s_idx - 1] == '/')
            {
                // remove unnecessary slashes
                if (c == '\0')
                {
                    d_idx -= 1;
                }
            }
            else if (s_idx > 2 && s[s_idx - 1] == '.'
                     && s[s_idx - 2] == '.' && s[s_idx - 3] == '/')
            {
                // evaluate "../" and "..\0"
                d_idx -= 3;
                while (--d_idx >= 0 && d[d_idx] != '/');
                if (d_idx < 0)
                {
                    fprintf(stderr, "Cannot 'cd ..' out of root! (%s)\n", d);
                    return NULL;
                }
                if (c != '\0')
                {
                    d_idx++;
                }
            }
            else if (s_idx > 1 && s[s_idx - 1] == '.' && s[s_idx - 2] == '/')
            {
                // remove "./" and ".\0"
                d_idx -= 1;
                if (c == '\0')
                {
                    d_idx -= 1;
                }
            }
            else
            {
                d[d_idx++] = c;
            }
            ++s_idx;
        }
        else
        {
            d[d_idx++] = s[s_idx++];
        }
    }

    if (d_idx != 0)
        d[d_idx] = '\0';

    else
        d[d_idx + 1] = '\0';

    return can_path;
}

int
main(void)
{
    int ret;
    int sfd;

    parse_grass();

    if ((sfd = create_socket()) < 0)
    {
        return sfd;
    }

    int int_port = atoi(port);
    if (int_port > (int)(uint16_t)(-1))
    {
        fprintf(stderr, "Port too large.\n");
    }

    uint16_t i_port = (uint16_t)int_port;

    if ((ret = bind_server_socket(sfd, &i_port)) < 0)
    {
        close(sfd);
        return ret;
    }

    while (1)
    {
        int nfd;
        if ((nfd = accept_connection(sfd, i_port)) > 0)
        {
            pthread_t th;
            int* fdp = malloc(sizeof(int));
            *fdp = nfd;
            pthread_create(&th, NULL, connection_thread, (void*)fdp);
        }
    }

    close(sfd);
    free_structs();

    return 0;
}
