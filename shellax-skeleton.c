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
      continue;                                          // empty arg, go for next
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
void ourUniq2(char *input);
int wiseman(struct command_t *command, char *minutes);
void chatroom(struct command_t *command);
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
  // if(strcmp(command->name, "wiseman") == 0)
  //   {
  //     command->background = true;

  //   }

  int connection[2];
  char message[4096];
  char message2[4096];

  if (pipe(connection) == -1)
  {
    printf("Pipe failed\n");
  }

  pid_t pid = fork();
  if (pid == 0) // child
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

    // if(strcmp(command->next->name,"uniq") == 0) {
    //   printf("Next is uniq\n");
    //   // pipeCommand(command,p);

    //   return SUCCESS;
    // }
    if (strcmp(command->name, "chatroom") == 0)
    {
      chatroom(command);
      exit(0);
    }

    if (strcmp(command->name, "wiseman") == 0)
    {
      wiseman(command, command->args[0]);
      exit(0);
    }

    if (command->next != NULL)
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

    char *path = getenv("PATH");
    // printf("Path: %s\n",path);

    char pathOfCommand[50];
    // pathOfCommand = (char*)malloc(150);

    const char s[2] = ":";
    char *token;

    token = strtok(path, s);
    // printf("Token:%s\n", token);

    DIR *chatroomPtr;
    struct dirent *entry;

    while (token != NULL)
    {
      chatroomPtr = opendir(token);

      if (chatroomPtr == NULL)
      {
        // return 1;
      }
      else
      {
        while ((entry = readdir(chatroomPtr)) != NULL)
        {
          if (strcmp(entry->d_name, command->name) == 0)
          {
            strcpy(pathOfCommand, "");
            strcpy(pathOfCommand, token);
            strcat(pathOfCommand, "/");
            strcat(pathOfCommand, command->name);
            // close(connection[0]);
            // close(connection[1]);
            execv(pathOfCommand, command->args);
            exit(0);
            break;
          }
          if (command->redirects[1] != NULL || command->redirects[2] != NULL) //---------Check if redirects
          {
            dup2(connection[1], STDOUT_FILENO);
          }
        }
      }

      token = strtok(NULL, s);
    }
  }
  else
  {
    // TODO: implement background processes here
    if (!command->background) //-----------------------------Background
    {
      wait(0);
    }
    // cat output2 >outputfile
    // cat output2 >>outputfile
    if (command->redirects[0] != NULL)
    { //-------------------------------- Redirects
      FILE *inputFile;
      inputFile = fopen(command->redirects[0], "r");
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
      if (command->redirects[1] != NULL)
      {
        close(connection[1]);
        read(connection[0], &message, sizeof(message));
        FILE *ptr;
        ptr = fopen(command->redirects[1], "w");
        printf("%s\n", command->redirects[1]);
        fprintf(ptr, "%s%s", message, message2);
        fclose(ptr);
        close(connection[1]);
      }
      if (command->redirects[2] != NULL)
      {
        close(connection[1]);
        read(connection[0], &message, sizeof(message));
        FILE *ptr;
        ptr = fopen(command->redirects[2], "a");
        printf("%s\n", command->redirects[2]);
        fputs(message, ptr);
        fprintf(ptr, "%s", message2);
        // fwrite(try,1,  sizeof(try) - 1, ptr);
        fclose(ptr);
        close(connection[1]);
      }
    }
    else
    {
      if (command->redirects[1] != NULL)
      {
        close(connection[1]);
        read(connection[0], &message, sizeof(message));
        FILE *ptr;
        ptr = fopen(command->redirects[1], "w");
        printf("%s\n", command->redirects[1]);
        fprintf(ptr, "%s", message);
        // fwrite(try,1,  sizeof(try) - 1, ptr);
        fclose(ptr);
        close(connection[1]);
      }
      if (command->redirects[2] != NULL)
      {
        close(connection[1]);
        read(connection[0], &message, sizeof(message));
        FILE *ptr;
        ptr = fopen(command->redirects[2], "a");
        printf("%s\n", command->redirects[2]);
        fputs(message, ptr);
        // fwrite(try,1,  sizeof(try) - 1, ptr);
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
  // int p[2];
  // pipe(p);
  char a[4096];
  if (strcmp(command->name, "uniq") == 0)
  {
    close(p[1]);
    read(p[0], &a, sizeof(a));
    if (command->arg_count > 0)
    {
      ourUniq2(a);
    }
    else
    {
      ourUniq(a);
    }
  }
  if (command->next == NULL)
  {
    close(0);
    dup(p[0]);
    close(p[1]);
    runCommand(command);
  }
  else
  {
    if (fork() == 0)
    {
      close(0);

      dup(p[0]);
      close(p[1]);
      pipeCommand(command->next, p);
    }
    else
    {
      close(1);
      dup(p[1]);
      close(p[0]);
      runCommand(command);
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

  char *path = getenv("PATH");
  char pathOfCommand[50];
  const char s[2] = ":";
  char *token;
  token = strtok(path, s);
  DIR *chatroomPtr;
  struct dirent *entry;

  while (token != NULL)
  {
    chatroomPtr = opendir(token);

    if (chatroomPtr == NULL)
    {
      // return 1;
    }
    else
    {
      while ((entry = readdir(chatroomPtr)) != NULL)
      {
        if (strcmp(entry->d_name, command->name) == 0)
        {
          strcpy(pathOfCommand, "");
          strcpy(pathOfCommand, token);
          strcat(pathOfCommand, "/");
          strcat(pathOfCommand, command->name);
          // close(connection[0]);
          // close(connection[1]);
          execv(pathOfCommand, command->args);
          exit(0);
          break;
        }
      }
    }

    token = strtok(NULL, s);
  }
}

void ourUniq(char *input)
{

  // strcat(input,"\n");
  const char s[2] = "\n";
  char *token;
  token = strtok(input, s);

  char visited[100][100];
  int i = 0;
  strcpy(visited[i], token);

  int j = 0;

  while (token != NULL)
  {
    for (int k = 0; k < i; k++)
    {
      if (strcmp(visited[k], token) == 0)
      {
        j = 1;
      }
    }
    if (j == 0)
    {
      strcpy(visited[i], token);
      i++;
    }
    token = strtok(NULL, s);
    j = 0;
  }

  for (int m = 0; m < i; m++)
  {
    printf("%s\n", visited[m]);
  }
}

void ourUniq2(char *input)
{

  const char s[2] = "\n";
  char *token;
  token = strtok(input, s);

  char visited[100][100];
  int visitedCount[100];
  int i = 0;
  strcpy(visited[i], token);

  int j = 0;

  while (token != NULL)
  {
    for (int k = 0; k < i; k++)
    {
      if (strcmp(visited[k], token) == 0)
      {
        j = 1;
        visitedCount[k]++;
      }
    }
    if (j == 0)
    {
      strcpy(visited[i], token);
      visitedCount[i] = 1;
      i++;
    }
    token = strtok(NULL, s);
    j = 0;
  }

  for (int m = 0; m < i; m++)
  {
    printf("%d %s\n", visitedCount[m], visited[m]);
  }
}

int wiseman(struct command_t *command, char *minutes){
  char str[150];
  strcpy(str, "echo '*/");
  strcat(str, minutes);
  strcat(str, " * * * * /tmp/com.sh' | crontab -");
  printf("str: %s\n", str);
  system("echo 'echo 'Wiseman is working' >>/tmp/wisecow.txt' >/tmp/com.sh");
  // system("echo 'echo fortune | cowsay >>/tmp/wisecow.txt' >/tmp/com.sh");
  system("chmod u+x /tmp/com.sh");
  system(str);
  return SUCCESS;
}

void chatroom(struct command_t *command)
{

  printf("Chatroom name: %s\n", command->args[0]);
  printf("User: %s\n", command->args[1]);

  int chatroomExist = 0;
  int userExist = 0;
  char chatroomName[50];
  char userName[50];

  int numberOfUser = 0;

  DIR *chatroomPtr;
  DIR *userPtr;
  struct dirent *entry;

  char users[50][50];

  strcpy(chatroomName, "/tmp/");
  strcat(chatroomName, command->args[0]);

  strcpy(userName, chatroomName);
  strcat(userName, "/");
  strcat(userName, command->args[1]);

  chatroomPtr = opendir("/tmp");

  if (chatroomPtr == NULL)
  {
    // return 1;
  }
  else
  {
    while ((entry = readdir(chatroomPtr)) != NULL)
    {
      if (strcmp(entry->d_name, command->args[0]) == 0)
      {
        chatroomExist = 1;
      }
    }
  }
  if (chatroomExist == 0)
  {
    mkdir(chatroomName, 0777);
  }
  else
  {
    userPtr = opendir(chatroomName);

    if (userPtr == NULL)
    {
      // return 1;
    }
    else
    {
      while ((entry = readdir(userPtr)) != NULL)
      {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
          // printf("skipped\n");
        }
        else
        {
          char username[50];
          strcpy(username, chatroomName);
          strcat(username, "/");
          strcat(username, entry->d_name);
          strcpy(users[numberOfUser], username);
          numberOfUser++;
          // printf("entryname: %s numberofuser: %d \n",entry->d_name, numberOfUser);
          if (strcmp(entry->d_name, command->args[1]) == 0)
          {
            userExist = 1;
          }
        }
      }
    }
  }

  if (userExist == 0)
  {
    // char username[50];
    // strcpy(username, chatroomName);
    // strcat(username, "/");
    // strcat(username, command->args[1]);
    strcpy(users[numberOfUser], userName);
    numberOfUser++;
    if (mkfifo(userName, 0777) == -1)
    {
      printf("Failed to pipe\n");
    }
  }

  pid_t pids[numberOfUser];
  char messageSent[450];
  char messageReceived[450];
  // strcpy(messageSent, "hello");

  for (int i = 0; i < numberOfUser; i++)
  {
    // printf("users[%d] : %s\n", i, users[i]);
    pid_t pid = fork();
    if (pid == 0) // child
    {
      strcpy(messageSent, "[");
      strcat(messageSent, command->args[0]);
      strcat(messageSent, "] ");
      strcat(messageSent, users[i]);

      int fd = open(users[i], O_WRONLY);
      write(fd, &messageSent, sizeof(messageSent));
      close(fd);

      kill(getpid(), SIGTERM);
    }
    else
    {
    }
  }

  printf("Welcome to %s!\n", command->args[0]);
  while (1)
  {
    int fd = open(userName, O_RDONLY);
    read(fd, &messageReceived, sizeof(messageReceived));
    close(fd);
    printf("%s\n", messageReceived);
    printf("%s write your message here:\n", command->args[1]);
    /* code */
  }
}
