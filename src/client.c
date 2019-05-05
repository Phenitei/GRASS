/**
 * @file client.c
 *
 * @brief Client for the GRASS protocol.
 *
 */

#include <assert.h>
#include <errno.h>
#include <client.h>
#include <grass.h>
#include <netinet/in.h>
#include <limits.h>

static int sockfd;
static struct shell_map* last_cmd = NULL;
static char* filename = NULL;
static char* address = NULL;
static size_t file_len = 0;

static FILE* infile = NULL;
static FILE* outfile = NULL;

int
main(int argc, char **argv)
{
    int _sockfd, err;
    uint16_t d_port;
    long int parsed_port = -1;

    if(argc != 3 && argc != 5)
    {
        fprintf(stderr, "Invalid number of arguments\n");
        return 1;
    }

    if((_sockfd = create_socket()) < 0)
        return _sockfd;
    else
        sockfd = _sockfd;


    if((parsed_port = strtol(argv[2], NULL, 10)) < 1 || parsed_port > USHRT_MAX)
    {
        fprintf(stderr, "Invalid port number: not a number, or larger than %hu", USHRT_MAX);
        return 1;
    }
    else
        d_port = (uint16_t) parsed_port;

    if((err = connect_to(sockfd, argv[1], d_port)))
        return err;

    if((address = calloc(strlen(argv[1]), sizeof(char))) == NULL)
    {
        perror("Cannot allocate the address pointer: ");
        return ENOMEM;
    }
    else
        strcpy(address, argv[1]);

    if (argc == 5)
    {
        infile = fopen(argv[3], "r");
        if (infile == NULL)
        {
            fprintf(stderr, "Impossible to open input file %s\n", argv[3]);
            return 1;
        }
        outfile = fopen(argv[4], "w");
        if (outfile == NULL)
        {
            fprintf(stderr, "Impossible to open output file %s\n", argv[4]);
            return 1;
        }
    }
    else
    {
        infile = stdin;
        outfile = stdout;
    }

    repl();

    sleep(1);

    fclose(infile);
    fclose(outfile);

    if(filename)
        free(filename);

    if(address)
        free(address);

    return 0;
}

/**
 * Execute a Read-Eval-Print loop on the given input.
 *
 * @return 0 if all went well, an error code otherwise.
 */
int
repl(void)
{
    char    typedstr[MAX_CHAR_LEN];
    ssize_t sh_argc = 0;
    ssize_t errcp   = 0;

    /* Because we dynamically allocate memory for the token array and for each
     * token, we need a pointer to the array of tokens to pass by reference */
    char ***tokenizedstr = calloc(1, sizeof( char** ));

    memset( &typedstr, 0, sizeof(typedstr));

    /* REPL part */
    while(!feof(infile))
    {
        fprintf(outfile, "> ");

        /* This happens IFF we encounter EOF and nothing else: user typed ^D */
        if (fgets(typedstr, MAX_CHAR_LEN, infile) == NULL)
        {
            free(tokenizedstr);
            return 0;
        }

        /* Split the input into tokens, and deal with errors in the if body */
        if (( sh_argc = tokenize_input(typedstr, tokenizedstr)) < 1)
        {
            free_tokenized(*tokenizedstr, 0);
            if(err_deal((int)((sh_argc == 0) ? ERR_SH_ICMD : sh_argc )))
                break;
            else
                continue;
        }

        errcp = run_cmd(*tokenizedstr, (size_t) sh_argc);
        free_tokenized(*tokenizedstr, (size_t) sh_argc);
        *tokenizedstr = NULL;

        if( errcp && (err_deal((int) errcp)))
            break;
    }

    free(tokenizedstr);
    tokenizedstr = NULL;

    return 0;
}

/**
 * @brief Splits up the passed input into words in the passed output pointer,
 * returning it's size.
 *
 * @param givenstr A null-terminated string that will be tokenized.
 * @param outputstrs Pointer to the array we will dynamically fill.
 *
 * This super-awesome function does some badass realloc-related shit to grow
 * outputstrs dynamically with the number of tokens, and also allocates just
 * enough memory for each of the tokens. This is rad but means *YOU* *MUST*
 * *CALL* *FREE*. Use the free_tokenized function. It's also not very
 * time-efficient to realloc so much, but this was written as an exercise for
 * memory usage.
 *
 * @return The number of parsed tokens if all went well, and < 0 otherwise.
 */
ssize_t
tokenize_input( char* givenstr, char** * const outputstrs )
{
    char*   token;
    char*   alloc_tmp_p1;
    char**  alloc_tmp_p2;
    ssize_t i = 0;

    /* Remove trailing newline */
    givenstr[strlen( givenstr ) - 1] = '\0';

    /* Cut the input into words */
    while(( token = strtok( givenstr, " " )) != NULL )
    {
        /* Required after the first call to strtok to continue parsing */
        givenstr = NULL;

        /* Allocate memory in our array for the pointer to this token */
        if(( alloc_tmp_p2 = realloc( *outputstrs,
                                     (( size_t )i + 1 ) * sizeof( char* ))) == NULL )
        {
            /* This happening is enough cause for concern that we die if we are
             * debugging */
            assert( alloc_tmp_p2 != NULL );
            return ENOMEM;
        }
        else
        {
            *outputstrs = alloc_tmp_p2;
            alloc_tmp_p2 = NULL;
        }

        /* Allocate memory to store the string outside this scope */
        if(( alloc_tmp_p1 = calloc( strlen( token ) + 1, sizeof( char ))) == NULL )
        {
            /* See above */
            assert( alloc_tmp_p1 != NULL );
            return ENOMEM;
        }
        else
        {
            ( *outputstrs )[i] = alloc_tmp_p1;
            alloc_tmp_p1 = NULL;
        }

        /* Pointer storage. */
        strcpy(( *outputstrs )[i], token );

        ++i;
    }

    return i;
}

/**
 * @brief Free the memory allocated to a tokenized input run.
 *
 * @param tokenized_array The array we want to free.
 * @param array_len The size of the above-mentioned array.
 */
void
free_tokenized(char **tokenized_array, size_t array_len)
{
    size_t i;

    if(tokenized_array != NULL)
    {
        for( i = 0; i < array_len; i++)
        {
            free(tokenized_array[i]);
            tokenized_array[i] = NULL;
        }
    }

    free(tokenized_array);
    tokenized_array = NULL;
}

/**
 * @brief Prints the correct error message associated with an error code.
 *
 * @param errcode The error code we want to handle.
 *
 * @return 0 on success. !0 requres program ending (fatal error or quit
 * request).
 */
int
err_deal(int errcode)
{
    switch(errcode)
    {
    /* We were asked to quit the program */
    case BYE_BYE:
        return BYE_BYE;

    case ERR_KILLED:
        return ERR_KILLED;

    /* Normal error codes */
    case ERR_SH_ICMD:
        fprintf(outfile, "ERROR: %s\n", ERR_SH_ICMD_STR);
        break;

    case ERR_SH_WARGC:
        fprintf(outfile, "ERROR: %s\n", ERR_SH_WARGC_STR);
        break;

    default: /* external error, use errno */
        return BYE_BYE;
    }

    fflush(outfile);

    return 0;
}

/**
 * @brief Run a command with passed arguments after checking aforesaid
 * arguments and command existence.
 *
 * @param cmd_and_args Tokenized array of user input.
 * @param asize Size of above array.
 *
 * @return 0 on success, < 0 on error.
 */
int
run_cmd(char** cmd_and_args, size_t asize)
{
    size_t  i = 0;
    int     errcp = 0;

    if(!cmd_and_args)
        return EINVAL;

    for( i = 0; i < NUM_COMMANDS; i++)
    {
        if(!(errcp = strncmp(cmd_and_args[0], shell_cmds[i].name, MAX_CHAR_LEN)))
        {
            last_cmd = &(shell_cmds[i]);
            return shell_cmds[i].fct(asize - 1, cmd_and_args);
        }
    }

    return ERR_SH_ICMD;
}

/**
 * Send a command to the server that produces no output.
 *
 * @param argc The number of arguments the command has.
 * @param cmd_and_args The command and arguments.
 * @return 0 if all went well, an error code otherwise.
 */
int
do_simple(size_t argc, char **cmd_and_args)
{
    size_t i;
    // Size of sent string.
    size_t len = 0;
    for(i = 0; i < argc + 1; ++i)
        len += strlen(cmd_and_args[i]) + 1;

    char *buffer = calloc(len, sizeof(char));
    strcpy(buffer, cmd_and_args[0]);

    for(i = 0; i < argc; ++i)
    {
        strcat(buffer, CMD_DELIM);
        strcat(buffer, cmd_and_args[i + 1]);
    }

    send(sockfd, buffer, len, 0);
    free(buffer);

    buffer = calloc(MAX_CHAR_LEN, sizeof(char));
    buffer[MAX_CHAR_LEN] = '\0';

    recv(sockfd, buffer, MAX_CHAR_LEN, 0);

    if (strncmp(buffer, "get port: ", 10) == 0
            && last_cmd != NULL
            && strcmp(last_cmd->name, "get") == 0)
    {
        // port number
        char* ptr = buffer + 10;
        while (*(++ptr) != ' ');
        *ptr = '\0';
        ptr += 7;
        uint16_t port_i = (uint16_t)atoi(buffer + 10);

        // file size
        char* end_ptr;
        file_len = strtoul(ptr, &end_ptr, 10);

        return handle_get(port_i);
    }
    else if (strncmp(buffer, "put port: ", 10) == 0
             && last_cmd != NULL
             && strcmp(last_cmd->name, "put") == 0)
    {
        uint16_t port_i = (uint16_t)atoi(buffer + 10);
        return handle_put(port_i);
    }

    if(strcmp(buffer, "OK") != 0)
        fprintf(outfile, "%s\n", buffer);

    /* If we have a server-side error, we printed it so for the client
     * everything went ok, hence the 0. */
    return 0;
}

/**
 * @brief Execute the `exit` command.
 *
 * @param discarded Discarded.
 * @return The code requesting the close of the REPL.
 */
int
do_exit(size_t discard1, char **discard2)
{
    (void) discard1;
    (void) discard2;
    return BYE_BYE;
}
int
do_get(size_t argc, char **argv)
{
    if(argc != 1)
        return ERR_SH_WARGC;

    if (filename != NULL)
        free(filename);

    filename = malloc(sizeof(char) * strlen(argv[1]));
    if (filename == NULL)
        return ERR_NO_MEM;

    strcpy(filename, argv[1]);

    return do_simple(argc, argv);
}

int
do_put(size_t argc, char **argv)
{
    if(argc != 2)
        return ERR_SH_WARGC;

    // extract filename
    if (filename != NULL)
        free(filename);

    filename = malloc(sizeof(char) * strlen(argv[1]));
    if (filename == NULL)
        return ERR_NO_MEM;

    strcpy(filename, argv[1]);

    // extract file len
    char* end_ptr;
    file_len = strtoul(argv[2], &end_ptr, 10);

    return do_simple(argc, argv);
}

int
handle_get(uint16_t port)
{
    FILE* f = fopen(filename, "w");
    if (f == NULL)
    {
        fprintf(outfile, "Error: Impossible to open file\n");
        return EINVAL;
    }

    return start_transfer_thread(f, port, recv_file);
}

int
handle_put(uint16_t port)
{
    FILE* f = fopen(filename, "r");
    if (f == NULL)
    {
        fprintf(outfile, "Error: Impossible to open file\n");
        return EINVAL;
    }

    return start_transfer_thread(f, port, send_file);
}

int
start_transfer_thread(FILE* f, uint16_t port, upon_transfer_start transferer)
{
    int fd;
    if ((fd = create_socket()) < 0)
        return fd;

    transfer_t* tr = malloc(sizeof(transfer_t));
    if (tr == NULL)
        return ENOMEM;

    tr->sockfd = fd;
    tr->file = f;
    tr->filename = filename;
    tr->file_len = file_len;
    tr->port = port;
    tr->transfer = transferer;

    pthread_t th;
    pthread_create(&th, NULL, transfer_thread, (void*)tr);

    return 0;
}

void*
transfer_thread(void* argvp)
{
    transfer_t* tr = (transfer_t*)argvp;

    int ret;
    int fd = tr->sockfd;
    FILE* f = tr->file;
    const char* f_name = tr->filename;
    size_t f_len = tr->file_len;
    uint16_t tr_port = tr->port;

    if ((ret = connect_to(fd, address, tr_port)) >= 0)
        tr->transfer(f_name, f, f_len, fd, outfile);

    close(fd);
    fclose(f);

    return NULL;
}
