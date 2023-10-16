# Shellax - An Interactive Unix-Style Operating System Shell

Shellax is an interactive Unix-style operating system shell written in C. It allows users to enter and execute commands, including both built-in commands and system programs. This project is divided into four main parts, each with specific functionalities and features.

## Part I - Basic Shell Features 
- Shellax supports basic command execution.
- It reads user commands, parses them, and separates them into distinct arguments.
- Command line inputs, except for built-in commands, are interpreted as program invocations.
- Background execution is supported by appending an ampersand (&) at the end of a command line.
- It uses the `execv()` system call for executing Linux programs and user programs.

## Part II - I/O Redirection and Piping
- Shellax implements I/O redirection for output (> and >>) and input (<).
- The '>' character creates or truncates an output file, while '>>' appends to the output file.
- The '<' character specifies input from a file.
- The `dup()` and `dup2()` system calls are used for I/O redirection.
- Shellax handles program piping, allowing the output of one command to serve as input to another.

## Part III - New Built-In Commands 
(a) `uniq`: Implemented in C, this command is similar to UNIX's `uniq` command. Given sorted lines, it prints unique values without duplicates. It supports the `-c` or `--count` option to prefix unique lines with the number of occurrences.

(b) `chatroom <roomname> <username>`: This command creates a simple group chat using named pipes. Users are represented by named pipes with their names, and rooms are represented by folders containing the named pipes of users who joined. Users can send and receive messages within a room.

(c) `wiseman <minutes>`: This command utilizes the `espeak` text-to-speech synthesizer and the `fortune` program to say random adages at specified intervals.

(d) Custom Command: You are encouraged to create a new custom Shellax command. Be creative and implement a unique functionality not found in traditional Unix shells.

## Getting Started
- To run Shellax, compile the provided source code and execute the resulting binary.
- Follow the command syntax and usage guidelines for each built-in command.

