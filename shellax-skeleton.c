#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
const char *sysname = "shellax";

enum return_codes
{
    SUCCESS = 0,
    EXIT = 1,
    UNKNOWN = 2,
};

struct command_t
{
    char *name;
    bool background;
    bool auto_complete;
    int arg_count;
    char **args;
    char *redirects[3];     // in/out redirection
    struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
    int i = 0;
    printf("Command: <%s>\n", command->name);
    printf("\tIs Background: %s\n", command->background ? "yes" : "no");
    printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
    printf("\tRedirects:\n");
    for (i = 0; i < 3; i++)
        printf("\t\t%d: %s\n", i,
               command->redirects[i] ? command->redirects[i] : "N/A");
    printf("\tArguments (%d):\n", command->arg_count);
    for (i = 0; i < command->arg_count; ++i)
        printf("\t\tArg %d: %s\n", i, command->args[i]);
    if (command->next)
    {
        printf("\tPiped to:\n");
        print_command(command->next);
    }
}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
    if (command->arg_count)
    {
        for (int i = 0; i < command->arg_count; ++i)
            free(command->args[i]);
        free(command->args);
    }
    for (int i = 0; i < 3; ++i)
        if (command->redirects[i])
            free(command->redirects[i]);
    if (command->next)
    {
        free_command(command->next);
        command->next = NULL;
    }
    free(command->name);
    free(command);
    return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
    char cwd[1024], hostname[1024];
    gethostname(hostname, sizeof(hostname));
    getcwd(cwd, sizeof(cwd));
    printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
    return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
    const char *splitters = " \t"; // split at whitespace
    int index, len;
    len = strlen(buf);
    while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
    {
        buf++;
        len--;
    }
    while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
        buf[--len] = 0; // trim right whitespace

    if (len > 0 && buf[len - 1] == '?') // auto-complete
        command->auto_complete = true;
    if (len > 0 && buf[len - 1] == '&') // background
        command->background = true;

    char *pch = strtok(buf, splitters);
    if (pch == NULL)
    {
        command->name = (char *)malloc(1);
        command->name[0] = 0;
    }
    else
    {
        command->name = (char *)malloc(strlen(pch) + 1);
        strcpy(command->name, pch);
    }

    command->args = (char **)malloc(sizeof(char *));

    int redirect_index;
    int arg_index = 0;
    char temp_buf[1024], *arg;
    while (1)
    {
        // tokenize input on splitters
        pch = strtok(NULL, splitters);
        if (!pch)
            break;
        arg = temp_buf;
        strcpy(arg, pch);
        len = strlen(arg);

        if (len == 0)
            continue;                                        // empty arg, go for next
        while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
        {
            arg++;
            len--;
        }
        while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
            arg[--len] = 0; // trim right whitespace
        if (len == 0)
            continue; // empty arg, go for next

        // piping to another command
        if (strcmp(arg, "|") == 0)
        {
            struct command_t *c = malloc(sizeof(struct command_t));
            int l = strlen(pch);
            pch[l] = splitters[0]; // restore strtok termination
            index = 1;
            while (pch[index] == ' ' || pch[index] == '\t')
                index++; // skip whitespaces

            parse_command(pch + index, c);
            pch[l] = 0; // put back strtok termination
            command->next = c;
            continue;
        }

        // background process
        if (strcmp(arg, "&") == 0)
            continue; // handled before

        // handle input redirection
        redirect_index = -1;
        if (arg[0] == '<')
            redirect_index = 0;
        if (arg[0] == '>')
        {
            if (len > 1 && arg[1] == '>')
            {
                redirect_index = 2;
                arg++;
                len--;
            }
            else
                redirect_index = 1;
        }
        if (redirect_index != -1)
        {
            command->redirects[redirect_index] = malloc(len);
            strcpy(command->redirects[redirect_index], arg + 1);
            continue;
        }

        // normal arguments
        if (len > 2 &&
            ((arg[0] == '"' && arg[len - 1] == '"') ||
             (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
        {
            arg[--len] = 0;
            arg++;
        }
        command->args =
            (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
        command->args[arg_index] = (char *)malloc(len + 1);
        strcpy(command->args[arg_index++], arg);
    }
    command->arg_count = arg_index;

    return 0;
}

void prompt_backspace()
{
    putchar(8);   // go back 1
    putchar(' '); // write empty over
    putchar(8);   // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
    int index = 0;
    char c;
    char buf[4096];
    static char oldbuf[4096];

    // tcgetattr gets the parameters of the current terminal
    // STDIN_FILENO will tell tcgetattr that it should write the settings
    // of stdin to oldt
    static struct termios backup_termios, new_termios;
    tcgetattr(STDIN_FILENO, &backup_termios);
    new_termios = backup_termios;
    // ICANON normally takes care that one line at a time will be processed
    // that means it will return if it sees a "\n" or an EOF or an EOL
    new_termios.c_lflag &=
        ~(ICANON |
          ECHO); // Also disable automatic echo. We manually echo each char.
    // Those new settings will be set to STDIN
    // TCSANOW tells tcsetattr to change attributes immediately.
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

    show_prompt();
    buf[0] = 0;
    while (1)
    {
        c = getchar();
        // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

        if (c == 9) // handle tab
        {
            buf[index++] = '?'; // autocomplete
            break;
        }

        if (c == 127) // handle backspace
        {
            if (index > 0)
            {
                prompt_backspace();
                index--;
            }
            continue;
        }

        if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68)
        {
            continue;
        }

        if (c == 65) // up arrow
        {
            while (index > 0)
            {
                prompt_backspace();
                index--;
            }

            char tmpbuf[4096];
            printf("%s", oldbuf);
            strcpy(tmpbuf, buf);
            strcpy(buf, oldbuf);
            strcpy(oldbuf, tmpbuf);
            index += strlen(buf);
            continue;
        }

        putchar(c); // echo the character
        buf[index++] = c;
        if (index >= sizeof(buf) - 1)
            break;
        if (c == '\n') // enter key
            break;
        if (c == 4) // Ctrl+D
            return EXIT;
    }
    if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
        index--;
    buf[index++] = '\0'; // null terminate string

    strcpy(oldbuf, buf);

    parse_command(buf, command);

    // print_command(command); // DEBUG: uncomment for debugging

    // restore the old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
    return SUCCESS;
}

int process_command(struct command_t *command);
int pipeCommand(struct command_t *command, int *p);
void runCommand(struct command_t *command);
void ourUniq(char *input);
void ourUniqWithCount(char *input);
int wiseman(struct command_t *command, char *minutes);
void chatroom(struct command_t *command);
void sendMessage(char *inputMessage, char users[50][50], int numUsers);
void guessGame(int guess, int goal, int lower, int higher, int *shot);
void wordGame(char word[], int *chance);
// helper functions to color texts in word game:
void printGameInfo();
void red();
void purple();
void green();
void blue();
void yellow();
void cyan();
void reset();

int main()
{
    while (1)
    {
        struct command_t *command = malloc(sizeof(struct command_t));
        memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

        int code;
        code = prompt(command);
        if (code == EXIT)
            break;

        code = process_command(command);
        if (code == EXIT)
            break;

        free_command(command);
    }

    printf("\n");
    return 0;
}

int process_command(struct command_t *command)
{
    int r;
    if (strcmp(command->name, "") == 0)
        return SUCCESS;

    if (strcmp(command->name, "exit") == 0)
        return EXIT;

    if (strcmp(command->name, "cd") == 0)
    {
        if (command->arg_count > 0)
        {
            r = chdir(command->args[0]);
            if (r == -1)
                printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
            return SUCCESS;
        }
    }

    int connection[2];
    char message[4096];
    char message2[4096];

    if (pipe(connection) == -1)
    {
        printf("Pipe failed\n");
    }

    pid_t pid = fork();
    if (pid == 0) // child process
    {
        /// This shows how to do exec with environ (but is not available on MacOs)
        // extern char** environ; // environment variables
        // execvpe(command->name, command->args, environ); // exec+args+path+environ

        /// This shows how to do exec with auto-path resolve
        // add a NULL argument to the end of args, and the name to the beginning
        // as required by exec

        // increase args size by 2

        int p[2];
        pipe(p);

        if (strcmp(command->name, "word") == 0) // custom command "word": a word guessing game
        {
            int chance = 6; // user has 6 chances to guess the word correctly

            srand(time(NULL));
            int randomNum = (rand() % 212) + 1; // generates a random number which will be used to choose the word for the game

            FILE *textfile;
            char word[7];

            textfile = fopen("words.txt", "r");
            if (textfile == NULL)
            {
                printf("Sorry, could not read.\n");
            }

            int count = 0;
            while (count != randomNum) // reads randomNum many lines from words.txt to find a word to play the game
            {
                fgets(word, 7, textfile);
                count++;
            }
            fclose(textfile);

            // prints the necessary information to play the game
            printGameInfo();

            // call the game with the selected word and the number of chances
            wordGame(word, &chance);
            exit(0);
        }

        if (strcmp(command->name, "guessGame") == 0) // custom command "guessGame"
        {
            if (command->arg_count != 1)
            {
                printf("Please enter valid arguments\n");
                exit(0);
            }
            else
            {
                int size = atoi(command->args[0]);
                srand(getpid()); // Initialization, should only be called once.
                int r = rand() % size;
                int firstGuess;
                int shott = 1;
                printf("Welcome to guess game please enter your first guess: ");
                scanf("%d", &firstGuess);
                guessGame(firstGuess, r, 0, size, &shott);
                exit(0);
            }
        }
        if (strcmp(command->name, "chatroom") == 0) // calls the chatroom function when the chatroom command is used
        {
            chatroom(command);
            exit(0);
        }

        if (strcmp(command->name, "wiseman") == 0) // calls the wiseman function when the wiseman command is used
        {
            wiseman(command, command->args[0]);
            exit(0);
        }

        if (command->next != NULL) // if command includes pipe, calls the pipeCommand function
        {
            pipeCommand(command, p);
        }

        command->args = (char **)realloc(
            command->args, sizeof(char *) * (command->arg_count += 2));

        // shift everything forward by 1
        for (int i = command->arg_count - 2; i > 0; --i)
            command->args[i] = command->args[i - 1];

        // set args[0] as a copy of name
        command->args[0] = strdup(command->name);
        // set args[arg_count-1] (last) to NULL
        command->args[command->arg_count - 1] = NULL;

        // TODO: do your own exec with path resolving using execv()
        // do so by replacing the execvp call below
        // execvp(command->name, command->args); // exec+args+path

        char *path = getenv("PATH"); // getting the path to find the given commands' paths.

        char pathOfCommand[50];

        const char s[2] = ":";
        char *token;

        // divide the path from each ":" to search every directory
        token = strtok(path, s);

        DIR *pDir;
        struct dirent *entry;

        while (token != NULL)
        {
            pDir = opendir(token);

            if (pDir == NULL)
            {
                // return 1;
            }
            else
            {
                while ((entry = readdir(pDir)) != NULL)
                {
                    if (strcmp(entry->d_name, command->name) == 0) // find the command's path
                    {
                        strcpy(pathOfCommand, "");
                        strcpy(pathOfCommand, token);
                        strcat(pathOfCommand, "/");
                        strcat(pathOfCommand, command->name); // pathOfCommand is the full path of the given command
                        execv(pathOfCommand, command->args);  // give the path of the command and the arguments to execv()
                        exit(0);
                        break;
                    }
                    if (command->redirects[1] != NULL || command->redirects[2] != NULL) //---------Check if redirects
                    {
                        dup2(connection[1], STDOUT_FILENO); // creates the copy of connection[1]
                    }
                }
            }

            token = strtok(NULL, s); // search the next directory
        }
    }
    else // parent process
    {
        // TODO: implement background processes here
        if (!command->background) //-----------------------------Background
        {
            wait(0); // wait for child process to finish, if the command is not running on the background
        }
        if (command->redirects[0] != NULL) // includes <
        {                                  //-------------------------------- Redirects
            FILE *inputFile;
            inputFile = fopen(command->redirects[0], "r"); // open the input file to read
            fseek(inputFile, 0, SEEK_END);
            int lenght = ftell(inputFile);
            fseek(inputFile, 0, SEEK_SET);
            char *message2 = (char *)malloc(sizeof(char) * (lenght + 1));
            char inp;
            message;
            int i = 0;
            while (1)
            {
                inp = fgetc(inputFile);
                if (feof(inputFile))
                {
                    break;
                }
                message2[i] = inp;
                i++;
            }
            message2[i] = '\0';
            if (command->redirects[1] != NULL) // includes >
            {
                close(connection[1]);                           // close the unused end of the file
                read(connection[0], &message, sizeof(message)); // read the input from the pipe
                FILE *ptr;
                ptr = fopen(command->redirects[1], "w"); // open the given file to write
                printf("%s\n", command->redirects[1]);
                fprintf(ptr, "%s%s", message, message2);
                fclose(ptr);
                close(connection[1]);
            }
            if (command->redirects[2] != NULL) // includes >>
            {
                close(connection[1]);                           // close the unused end of the file
                read(connection[0], &message, sizeof(message)); // read the input from the pipe
                FILE *ptr;
                ptr = fopen(command->redirects[2], "a"); // option "a" appends to the end of the file
                printf("%s\n", command->redirects[2]);
                fputs(message, ptr);
                fprintf(ptr, "%s", message2);
                fclose(ptr);
                close(connection[1]);
            }
        }
        else // no < operation
        {
            if (command->redirects[1] != NULL) // operation: >
            {
                close(connection[1]);                           // close the unused end of the pipe
                read(connection[0], &message, sizeof(message)); // read the input from the pipe
                FILE *ptr;
                ptr = fopen(command->redirects[1], "w"); // open to file to write
                printf("%s\n", command->redirects[1]);
                fprintf(ptr, "%s", message);
                fclose(ptr);
                close(connection[1]);
            }
            if (command->redirects[2] != NULL) // operation: >>
            {
                close(connection[1]);                           // close the unused end of the pipe
                read(connection[0], &message, sizeof(message)); // read the input from the pipe
                FILE *ptr;
                ptr = fopen(command->redirects[2], "a"); // option "a" appends to the end of the file
                printf("%s\n", command->redirects[2]);
                fputs(message, ptr);
                fclose(ptr);
                close(connection[1]);
            }
        }

        return SUCCESS;
    }

    // TODO: your implementation here
    printf("-%s: %s: command not found\n", sysname, command->name);
    return UNKNOWN;
}

int pipeCommand(struct command_t *command, int *p)
{
    char word[4096];
    if (strcmp(command->name, "uniq") == 0) // call the corresponding uniq function if the command is "uniq"
    {
        close(p[1]);
        read(p[0], &word, sizeof(word)); // get the input which "uniq" command will be applied to

        if (command->arg_count > 0) // handles uniq -c
        {
            ourUniqWithCount(word);
        }
        else // handles uniq
        {
            ourUniq(word);
        }
    }

    if (command->next == NULL) // base case for piping
    {
        close(0);
        dup(p[0]); // pass the output to the next command as an input
        close(p[1]);
        runCommand(command); // no more pipe, run the last commnand on the output of previous commands
    }
    else
    {
        if (fork() == 0)
        {
            close(0);
            dup(p[0]); // pass the output to the next command as an input
            close(p[1]);
            pipeCommand(command->next, p); // recursive call to handle the next pipe
        }
        else
        {
            close(1);
            dup(p[1]); // write to pipe - passing the input
            close(p[0]);
            runCommand(command); // run command with the input
        }
    }
    return 0;
}

void runCommand(struct command_t *command)
{
    // increase args size by 2
    command->args = (char **)realloc(
        command->args, sizeof(char *) * (command->arg_count += 2));

    // shift everything forward by 1
    for (int i = command->arg_count - 2; i > 0; --i)
        command->args[i] = command->args[i - 1];

    command->args[0] = strdup(command->name);
    command->args[command->arg_count - 1] = NULL;

    char *path = getenv("PATH"); // get the path to search for commands

    char pathOfCommand[50];
    const char s[2] = ":";

    char *token;

    token = strtok(path, s); // divide into tokens to search into every directory

    DIR *pDir;
    struct dirent *entry;

    while (token != NULL) // keep reading until there are no directories left
    {
        pDir = opendir(token); // open the current directory

        if (pDir == NULL)
        {
            // return 1;
        }
        else
        {
            while ((entry = readdir(pDir)) != NULL) // read the entries in the current directory
            {
                if (strcmp(entry->d_name, command->name) == 0) // the path of the command is found
                {
                    strcpy(pathOfCommand, "");
                    strcpy(pathOfCommand, token);
                    strcat(pathOfCommand, "/");
                    strcat(pathOfCommand, command->name);
                    execv(pathOfCommand, command->args); // call execv() wiht the path of the command and the arguments received from the user
                    exit(0);
                    break;
                }
            }
        }

        token = strtok(NULL, s); // read the next one
    }
}

void ourUniq(char *input)
{
    const char s[2] = "\n";
    char *token;
    token = strtok(input, s); // get each token separated by a newline

    char visited[100][100]; // keep an array of strings to keep the words encountered for the first time
    int i = 0;
    strcpy(visited[i], token); // the first word read from the input should be added to the array

    int exists = 0; // initially exists set to 0 meaning the word is not encountered yet

    while (token != NULL) // keep reading the input
    {
        for (int k = 0; k < i; k++)
        {
            if (strcmp(visited[k], token) == 0) // current token is a duplicate, so we will not add it to the visited array
            {
                exists = 1; // this word is previously encountered
            }
        }
        if (exists == 0) // first encounter of this word, so will be add to the array
        {
            strcpy(visited[i], token);
            i++;
        }
        token = strtok(NULL, s); // read the next one
        exists = 0;              // reset the exists variable
    }

    for (int m = 0; m < i; m++) // print the elements from the array with no duplicates
    {
        printf("%s\n", visited[m]);
    }
}

void ourUniqWithCount(char *input)
{
    const char s[2] = "\n";
    char *token;
    token = strtok(input, s); // get each token separated by a newline

    char visited[100][100]; // keep an array of strings to keep the words encountered for the first time
    int visitedCount[100];  // will keep the count in an array
    int i = 0;
    strcpy(visited[i], token); // the first word read from the input should be added to the array

    int exists = 0; // initially exists set to 0 meaning the word is not encountered yet

    while (token != NULL) // keep reading the input
    {
        for (int k = 0; k < i; k++)
        {
            if (strcmp(visited[k], token) == 0) // current token is a duplicate, so we will not add it to the visited array
            {
                exists = 1;        // this word is previously encountered
                visitedCount[k]++; // increment the corresponding index in the visitedCount array
            }
        }
        if (exists == 0) // first encounter of this word, so will be add to the array
        {
            strcpy(visited[i], token);
            visitedCount[i] = 1; // count is 1
            i++;
        }
        token = strtok(NULL, s); // read the next one
        exists = 0;              // reset the exists variable
    }

    for (int m = 0; m < i; m++) // print the elements from the array with no duplicates and the corresponding counts
    {
        printf("%d %s\n", visitedCount[m], visited[m]);
    }
}

int wiseman(struct command_t *command, char *minutes)
{
    // str will appends the input "minutes" to the cronjob to be scheduled
    char str[150];
    strcpy(str, "echo '*/");
    strcat(str, minutes);
    strcat(str, " * * * * /tmp/com.sh' | crontab -");
    // system("echo 'echo | fortune | cowsay >>/tmp/wisecow.txt' >/tmp/com.sh"); //does not work for some reason
    //  instead it writes "Wiseman is working" to /tmp/wisecow.txt periodically
    system("echo 'echo 'Wiseman is working' >>/tmp/wisecow.txt' >/tmp/com.sh");
    system("chmod a+x /tmp/com.sh"); // make the com.sh executable
    system(str);                     // schedule the cron job
    return SUCCESS;
}

void chatroom(struct command_t *command)
{
    printf("Chatroom name: %s\n", command->args[0]);
    printf("User: %s\n", command->args[1]);

    // these variables will be used to decide whether to create a new chatroom and/or a new user by checking already existing chatrooms and users
    int chatroomExist = 0;
    int userExist = 0;

    char chatroomName[50];
    char userName[50];

    int numberOfUser = 0; // keeping the number of users in a chatroom

    DIR *chatroomPtr;
    DIR *userPtr;
    DIR *userPtr2;
    DIR *userPtr3;
    struct dirent *entry;

    char users[50][50]; // an array of strings to keep all the users in a chatroom

    strcpy(chatroomName, "/tmp/");
    strcat(chatroomName, command->args[0]); // name of the chatroom -> name of the folder that we keep the users
    strcpy(userName, chatroomName);
    strcat(userName, "/");
    strcat(userName, command->args[1]); // append the name of the user

    chatroomPtr = opendir("/tmp");

    if (chatroomPtr == NULL)
    {
    }
    else
    {
        while ((entry = readdir(chatroomPtr)) != NULL) // check inside the /tmp
        {
            if (strcmp(entry->d_name, command->args[0]) == 0) // chatroom is found, no need to create again
            {
                chatroomExist = 1;
            }
        }
    }
    if (chatroomExist == 0) // chatroom is not created before
    {
        mkdir(chatroomName, 0777); // create the directory with the given name and handle the permissions
    }
    else
    {
        userPtr = opendir(chatroomName);

        if (userPtr == NULL)
        {
        }
        else
        {
            while ((entry = readdir(userPtr)) != NULL) // check inside the chatroom
            {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) // don't need to check these
                {
                }
                else
                {
                    char username[50];
                    strcpy(username, chatroomName);
                    strcat(username, "/");
                    strcat(username, entry->d_name);
                    strcpy(users[numberOfUser], username);
                    numberOfUser++;
                    if (strcmp(entry->d_name, command->args[1]) == 0) // user is created before
                    {
                        userExist = 1;
                    }
                }
            }
        }
    }

    if (userExist == 0) // user does not exist
    {
        strcpy(users[numberOfUser], userName); // add the new user's info to the users array
        numberOfUser++;
        if (mkfifo(userName, 0777) == -1) // create a named pipe with the given user name and handle the permissions
        {
            printf("Failed to pipe\n");
        }
    }

    // pid_t pids[numberOfUser];
    char messageSent[450];
    char messageReceived[450];
    strcpy(messageSent, "");

    for (int i = 0; i < numberOfUser; i++)
    {
        pid_t pid = fork();
        if (pid == 0) // child process
        {
            // print a message everytime someone new joined to the conversation
            strcat(messageSent, "[");
            strcat(messageSent, command->args[0]);
            strcat(messageSent, "] ");
            strcat(messageSent, command->args[1]);
            strcat(messageSent, ": ");
            strcat(messageSent, command->args[1]);
            strcat(messageSent, " joined!");

            // send a message by writing into every pipe in the chatroom:
            int fd = open(users[i], O_WRONLY);
            write(fd, &messageSent, sizeof(messageSent));
            close(fd);

            kill(getpid(), SIGTERM);
        }
        else
        {
        }
    }

    printf("Welcome to %s!\n", command->args[0]); // print a welcome message with the name of the chatroom

    while (1) // each user receives a message by continuously reading from their named pipe
    {
        char messageSent[450];
        char messageReceived[450];
        strcpy(messageSent, "");

        int fd1 = open(userName, O_RDONLY);
        read(fd1, &messageReceived, sizeof(messageReceived));
        close(fd1);

        printf("%s\n", messageReceived);
        printf("%s write your message here:\n", command->args[1]);

        // get the message from the user:
        char messageToSent[450];
        fgets(messageToSent, 450, stdin);

        for (int i = 0; i < numberOfUser; i++)
        {
            pid_t pid2 = fork();
            if (pid2 == 0) // child process
            {
                strcat(messageSent, "[");
                strcat(messageSent, command->args[0]);
                strcat(messageSent, "] ");
                strcat(messageSent, command->args[1]);
                strcat(messageSent, ": ");
                strcat(messageSent, messageToSent);

                int fd2 = open(users[i], O_WRONLY);
                write(fd2, &messageSent, sizeof(messageSent));
                close(fd2);

                kill(getpid(), SIGTERM);
            }
            else
            {
            }
        }
    }
}

// Custom Command - Tuna
void guessGame(int guess, int goal, int lower, int higher, int *shot)
{

    if (guess > higher || guess < lower)
    {
        printf("Invalid input \n");
        exit(0);
    }

    if (guess == goal)
    {
        printf("Welldone you found the goal in %d shots\n", *shot);
        exit(0);
    }

    if (guess < goal)
    {

        int newguess;
        printf("Too low please make a guess between %d-%d : ", guess, higher);
        (*shot)++;
        scanf("%d", &newguess);
        guessGame(newguess, goal, guess, higher, shot);
    }
    else if (guess > goal)
    {
        int newguess2;
        printf("Too high please make a guess between %d-%d : ", lower, guess);
        (*shot)++;
        scanf("%d", &newguess2);
        guessGame(newguess2, goal, lower, guess, shot);
    }
}

// Custom Command - Yesim
void wordGame(char word[], int *chance) // takes the word to be guessed in the game,  and the number of chances the user have
{
    // will count the number of letters that are in the right location
    // correctness == 5 means the word is guessed correctly
    int correctness = 0;

    char guess[7];

    // Get the guess from the user
    printf("Enter a guess: ");
    fgets(guess, 7, stdin);

    // Compare each char of the guess string and the wordle string
    for (int i = 0; i < 5; i++)
    {
        if (guess[i] == word[i]) // the letter user guessed is in the right location.
        {
            correctness++; // Increment each time a letter is right
            green();       // Print this letter green
            printf("%c", guess[i]);
            reset();
        }
        // Check if the current letter is same as any of the letters in the word. If not, it should be printed in red
        else if (guess[i] != word[0] && guess[i] != word[1] &&
                 guess[i] != word[2] && guess[i] != word[3] &&
                 guess[i] != word[4])
        {
            red();
            printf("%c", guess[i]);
            reset();
        }
        else // Letters that are in the word, but guessed in the wrong location will be colored yellow.
        {
            yellow();
            printf("%c", guess[i]);
            reset();
        }
    }
    printf("\n"); // After printing and coloring the guess string
    (*chance)--;  // User lost 1 chance, decrement the chance

    if (correctness == 5 && (*chance) >= 0) // all the letters are guessed correctly. Do not call the function recursively.
    {
        blue();
        printf("Correct!\n");
        reset();
    }
    else if (correctness > 2 && (*chance) > 0) // more than 2 letters are guessed correctly in the right location and the user still has chances
    {
        blue();
        printf("%d chances left. Almost there, try again.\n", *chance);
        reset();
        wordGame(word, chance);
    }
    else if ((*chance) > 0) // user still has chances, so call the function recursively, and print out the chances left.
    {
        blue();
        printf("%d chances left. Try again.\n", *chance);
        reset();
        wordGame(word, chance);
    }
    else if ((*chance) == 0) // no chances left, print out the actual word
    {
        blue();
        printf("Sorry:(( The word you were looking for : ");
        reset();
        cyan();
        printf("%s", word);
        reset();
    }
}

// helper function for the wordGame
void printGameInfo()
{
    purple();
    printf("Guess a 5 letter word. Do not use capital letters.");
    reset();
    green();
    printf("\nGreen letters: ");
    reset();
    printf("the letter is in the right place. ");
    reset();
    red();
    printf("\nRed letters: ");
    reset();
    printf("the word does not include those letters. ");
    reset();
    yellow();
    printf("\nYellow letters: ");
    reset();
    printf("the word includes those letters but in a different location.\n");
    reset();
    purple();
    printf("Good Luck!\n");
    reset();
}
// functions below this line are used to color the texts in the wordGame:
void red()
{
    printf("\033[1;31m");
}

void purple()
{
    printf("\033[1;35m");
}

void green()
{
    printf("\033[1;32m");
}

void reset()
{
    printf("\033[0m");
}

void blue()
{
    printf("\33[0;34m");
}

void yellow()
{
    printf("\033[1;33m");
}

void cyan()
{
    printf("\033[1;36m");
}
