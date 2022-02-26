// OSU CS 344 Fall 2021 - Assignment 3
// Author: Collin Gilmore


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

// Defining constants for user input parameters
#define MAX_LEN 4096 /* left room for null pointer so we can have a max input size of 2048 and for variable expansion */
#define MAX_ARGS 512 

// Set up global variables for keeping track of child processes, number of PIDs, and if running a program in the background is allowed
int lastForProc = 0;
int numOfPids = 0;
int backgroundEnabled = 0; // 0 = background enabled 

//      User Input Struct & Processing ----------------------------------------
//      Struct -----------------------------------------------------
// setup of the command parsed and placed into this struct
typedef struct shellCommand {
    int argc;           // num of args entered 
    int length;         // length of the commands entered 
    char *command;      // command entered (cd, ls, mkdir, etc.)
    char *argv;         // args passed (-l, -a, etc.)
    char *inputFile;    // name of input file for file redirection
    char *outputFile;   // name of input file for file redirection
    int inputChar;      // keeping track of the input character <
    int outputChar;     // keeping track of the input character >
    int backgroundChar; // keeping track if the background character was input &
    int status;         // for keeping track of a specific commands status
    char last_special;  // for keeping tack of the last special character (<, >, &)
} shellCommand;

// Creating a simple linked list to store PIDs that are currently running. 
// using a linked list so I can update and delete the list as i loop through it to check PIDS, makes it easier
// than using a regular array
typedef struct linkedList{
    pid_t data;
    struct linkedList* next;
}linkedList;

linkedList *head = NULL;
linkedList *curr = NULL;

// function to delete a node from the linked list - used in checking background PIDs 
void deleteNode(pid_t pid){
    linkedList *curr = head;
    linkedList *prev = NULL;

    if(head == NULL){
        return;
    }

    while(curr->data != pid){
        if (curr->next == NULL){
            printf("value does not exist in this list");
            return;
        }

        prev = curr;
        curr = curr->next;
    }

    if(curr == head){
        head = head->next;
    } else {
        prev->next = curr->next;
    }
}


//      Input Processing -------------------------------------------
shellCommand *processCommand(char *string){
    // Create a shellCommand struct where we will store the information we parse from it
    shellCommand *currCommand = malloc(sizeof(struct shellCommand));

    // Setup of pointers, token, and duplicating OG command
    currCommand->argv = calloc(strlen(string) + 1, sizeof(char));
    currCommand->command = calloc(strlen(string) + 1, sizeof(char));
    currCommand->inputFile = calloc(strlen(string) + 1, sizeof(char));
    currCommand->outputFile = calloc(strlen(string) + 1, sizeof(char));

    // Setting the default strings to empty because i concatenate onto them later to process multiple inputs
    // probably didnt need to do this for anything but argv but it keeps the methodology consistent
    strcpy(currCommand->command, "");
    strcpy(currCommand->argv, "");
    strcpy(currCommand->inputFile, "");
    strcpy(currCommand->outputFile, "");

    // Set all int values in the command struct to 0 as default, only changing them if we encounter them
    currCommand->status = 0;
    currCommand->argc = 0;
    currCommand->length = 0;
    currCommand->inputChar = 0;
    currCommand->outputChar = 0;
    currCommand->backgroundChar = 0;

    // set up pointers, dup original string so we don't change it in case we need it later
    // set up token with strtok_r
    char *savePtr;
    char *dupString = calloc(strlen(string) + 1, sizeof(char));
    strcpy(dupString, string);
    char *token = strtok_r(dupString, " ", &savePtr);

    // Snag the actual command portion and store it. Note we have already dealt with commments and 
    // blank inputs before this function ever gets called
    currCommand->command = calloc(strlen(token) + 1, sizeof(char));
    strcpy(currCommand->command, token);
    token = strtok_r(NULL, " ", &savePtr);

    // Now that we have processed the command, check for additional parameters (files, special characters, arguments)
    while (token != NULL){
        // check for input char
        if (strcmp(token, "<") == 0){
            currCommand->inputChar += 1;
            currCommand->last_special = '<';
            // check for output char
        } else if (strcmp(token, ">") == 0){
            currCommand->outputChar += 1;
            currCommand->last_special = '>';
        } else if (strcmp(token, "&") == 0){
            // check for background char
            currCommand->backgroundChar += 1;
            currCommand->last_special = '&';
            // check if current token is an argument and concatenate it on if so 
            // this will have to be parsed again to make an new argv[] or strings for execvp function
            // but this was created before i realized that, also its easier to do later with an argc value
        } else if (currCommand->inputChar == 0 && currCommand->outputChar == 0 ){
            strcat(currCommand->argv, token);
            strcat(currCommand->argv, " ");
            currCommand->argc += 1;
            // check for and store input file as a string
        } else if ((currCommand->inputChar == 1 && currCommand->outputChar == 0) || currCommand->last_special == '<' ){
            currCommand->inputFile = calloc(strlen(token) + 1, sizeof(char));
            strcat(currCommand->inputFile, token);
            // check for and store output file as a string
        } else if ((currCommand->inputChar == 1 && currCommand->outputChar == 1 && currCommand->backgroundChar == 0) || currCommand->last_special == '>' ){
            currCommand->outputFile = calloc(strlen(token) + 1, sizeof(char));
            strcat(currCommand->outputFile, token);
        } 

        // update the token
        token = strtok_r(NULL, " ", &savePtr);
    }

    // for debugging purposses

    // puts("______________________________________");
    // printf("Command:------- %s\n", currCommand->command);
    // printf("Argv:---------- %s\n", currCommand->argv);
    // printf("Argc:---------- %d\n", currCommand->argc);
    // printf("inputChar:----- %d\n", currCommand->inputChar);
    // printf("outputChar:---- %d\n", currCommand->outputChar);
    // printf("inputFile:----- %s\n", currCommand->inputFile);
    // printf("outputFile:---- %s\n", currCommand->outputFile);
    // printf("backgroundChar: %d\n", currCommand->backgroundChar);
    // puts("______________________________________");


    return currCommand;
}


//      BUILT-IN FUNCTIONALITY ------------------------------------------------

// takes a string as input, replaces all "$$" with the current pid and then returns an output string with
// the pid instead of the "$$"
char* variableExpansion(char* string, char* outputString){
    // Expands any instances of "$$" in the input string to the pid 
    char *dollar = "$$";

    // Get pid and store it in a string
    int pid = getppid();
    int length = snprintf(NULL, 0, "%d", pid);
    char *pidString = malloc(length + 1);
    snprintf(pidString, length + 1, "%d", pid);

    // find the first instance of "$$"
    char *instance = strstr(string, dollar);

    if (instance == NULL){
        return string;
    }

    // calculate the distance in the string from the instance of $$
    int distance = strlen(string) - strlen(instance);
    strncat(outputString, string, distance);
    strcat(outputString, pidString);

    // move the pointer by 2 to search for the next instance
    instance += 2;
    char* oldPointer = instance;
    instance = strstr(instance, dollar);

    // repeatedly find instances of $$ and replaces them
    while (instance != NULL){
        distance = strlen(oldPointer) - strlen(instance);
        strncat(outputString, oldPointer, distance);
        strcat(outputString, pidString);
        
        instance += 2;
        oldPointer = instance;
        instance = strstr(instance, dollar);
    } 

    // test test$ test$$ test$$$ test$$$$
    free(pidString);
    return outputString;
}

// Exits the parent process and terminates all jobs and child processes
void exitProgram(){
    exit(0);
}

void status(int status){
    // returns the exit value of the last terminated process
    // or returns the termination signal if terminated by a signal

    if (WIFEXITED(status)) {
        printf("exit value %d\n", WEXITSTATUS(status));
    } else {
        printf("terminated by signal %d\n", WTERMSIG(status));
    }
}

//      END BUILT-IN FUNCTIONALITY ---------------------------------------------------

void handle_SIGTSTP(int signo){
    // changing functionality off ^Z/SIGTSTP to enable and disable background processes from being ran
    if (backgroundEnabled == 0){
	char* message = "\nEntering foreground-only mode (& is now ignored)\n";
	write(STDOUT_FILENO, message, 50);
    backgroundEnabled = 1;
    signal(SIGTSTP, SIG_IGN);

    } else {
    char* message2 = "\nExiting foreground-only mode\n";
	write(STDOUT_FILENO, message2, 31);
    backgroundEnabled = 0;
    }
}

// allows for commands native to the bash shell
void externalCommand(shellCommand *command){
    int childStatus;

    // signal handling needed to be here and in the input prompt or else I couldn't 
    // set specific options for the parent and children processes
    // set up signal handling
    struct sigaction SIGTSTP_action = {{0}}, ignore_action = {{0}}, default_action = {{0}};
    // Set up handlers
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    ignore_action.sa_handler = SIG_IGN;
    ignore_action.sa_handler = SIG_DFL;

    // Register handlers
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    sigaction(SIGINT, &ignore_action, NULL);

    // Fork new process
    pid_t spawnPid = fork();

    switch(spawnPid){
        // error in forking a new process
        case -1:
            perror("fork() error: \n");
            exit(1);
            break;
        
        // This is the case in which the child process can run
        case 0:
            // Reset signal handler for SIGINT to default and ignore SIGTSTP
            sigaction(SIGTSTP, &ignore_action, NULL);
            
            if (backgroundEnabled == 0 && command->backgroundChar == 1){
                sigaction(SIGINT, &ignore_action, NULL);
            } else {
                sigaction(SIGINT, &default_action, NULL);
            }
            command->backgroundChar = 0;

            // Set input/output for background processes
            if (command->backgroundChar != 0){
                // Open source file
                int backgroundSourceFD = open("/dev/null", O_RDONLY);
                if (backgroundSourceFD == -1) { 
                    perror("source open()"); 
                    exit(1); 
	            }

                // dup2 is used here to redirect the input from stdin(0) to the file specified 
                int bgInput = dup2(backgroundSourceFD, 0);
                if (bgInput == -1){
                    perror("source dup2() input file");
                    exit(1);
                }

                // Open target file
                int backgroundTargetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (backgroundTargetFD == -1) { 
                    perror("source open()"); 
                    exit(1); 
	            }

                // dup2 is used here to redirect the input from stdout(1) to the file specified 
                int bgInput2 = dup2(backgroundTargetFD, 1);
                if (bgInput2 == -1){
                    perror("source dup2() output file");
                    exit(1);
                }
            }

            // Handle Redirect input file
            if (command->inputChar != 0){
                // Open source file
                int sourceFD = open(command->inputFile, O_RDONLY);
                if (sourceFD == -1) { 
                    perror("source open()"); 
                    exit(1); 
	            }

                int input = dup2(sourceFD, 0);
                if (input == -1){
                    perror("source dup2() input file");
                    exit(1);
                }
            }
            
            // Handle Redirect output file
            if (command->outputChar != 0){
                // Open target file
                int targetFD = open(command->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (targetFD == -1) { 
                    perror("source open()"); 
                    exit(1); 
	            }

                int input = dup2(targetFD, 1);
                if (input == -1){
                    perror("source dup2() output file");
                    exit(1);
                }
            }

            // parse argv into newargv[][] - an array of strings
            // argv is a string of args separated by spaces 
            // the next 15 lines or so are to setup newargv to be used in execvp
            char *savePtr;
            char *token = strtok_r(command->argv, " ", &savePtr);
            char **newargv = malloc((command->argc + 2)*sizeof(char *) + sizeof(NULL));

            newargv[0] = malloc(100 * sizeof(char));
            strcpy(newargv[0], command->command);
            
            int index = 1;
            while(token != NULL){
                newargv[index] = malloc(100 * sizeof(char));
                strcpy(newargv[index], token);
                index ++;
                token = strtok_r(NULL, " ", &savePtr);
            }

            newargv[index] = NULL;

            // pass commands and args to the exec functions allowing for native bash commands to be run
            if(strcmp(command->argv, "") == 0){
                execlp(command->command, command->command, NULL, NULL);
            } else {
                execvp(newargv[0], newargv);
            }
            // only prints if execvp/execlp encounter an error
            fprintf(stderr, "%s: ", command->command);
            perror("");
            exit(1);
            break;

        default:
            // Background Process
            if (command->backgroundChar == 1 && backgroundEnabled == 0){
                printf("background pid = %d\n", spawnPid); // print pid of most recent background process
                // add new pid to linked list so we can check for them compleeting later
                linkedList *node = (struct linkedList*) malloc(sizeof(struct linkedList));
                node->data = spawnPid;
                node->next = head;
                head = node;
                
            // Foreground process
            } else {
                // wait for foreground process to finish or to be terminated by a signal 
                spawnPid = waitpid(spawnPid, &childStatus, 0);
                if (WIFSIGNALED(childStatus)) {
                    printf("terminated by signal %d\n", WTERMSIG(childStatus));
                }
                // used for easy access in status function
                lastForProc = childStatus;
            }
            break;
    }
}


// --------------------- MAIN FUNCTION AND USER PROMPT ---------------------------
void promptInput(){
    // Get user input and set to max length defined in the assign instructions
    char input[MAX_LEN];
    int childStatus;

    // set up signal handling
    struct sigaction SIGTSTP_action = {{0}}, ignore_action = {{0}};
    // Set up handlers
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    ignore_action.sa_handler = SIG_IGN;

    // // Set up flags
    // SIGTSTP_action.sa_flags = SA_RESTART;

    // Register handlers
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    sigaction(SIGINT, &ignore_action, NULL);

    // create pointer to head of the linked list
    // loop over linked list and check each process to see if its done
    // delete any finished processes and print out a message about their exit value or termination signal
    linkedList *ptr = head;
    while (ptr != NULL){
        if (waitpid(ptr->data, &childStatus, WNOHANG) == ptr->data) {
            if (WIFEXITED(childStatus)) {
                printf("background pid %d is done: exit value %d\n", ptr->data, WEXITSTATUS(childStatus));
                deleteNode(ptr->data);
            } else {
                printf("background pid %d is done: terminated by signal %d\n", ptr->data, WTERMSIG(childStatus));
                deleteNode(ptr->data);
            }
        }

        ptr = ptr->next;

        if (head == NULL){
            break;
        }
    }
 
    // prompt user for input, store input and remove "/n"
    printf(": ");
    fgets(input, MAX_LEN, stdin);
    input[strcspn(input, "\n")] = 0;

    
    // Check if the input is a blank line or comment - if it is just re prompt the user
    // "10" is the equivalent to the new line key added when the user hits enter on fgets
    if (strcmp(input, "") == 0|| input[0] == '#'){
        promptInput();
        return;
    }

    // call variable expansion and process command functions
    char outputString[MAX_LEN] = "";
    strcpy(input, variableExpansion(input, outputString));
    shellCommand *command = processCommand(input);

    // get size of argv and remove trailing space value
    int argvSize = strlen(command->argv);
    command->argv[argvSize - 1] = '\0';

    if (strcmp(command->command, "exit") == 0){
        exitProgram();

    // Change Directory command 
    } else if (strncmp(command->command, "cd", 2) == 0) {
        // change directory
        // check if there are any arguments, if not change to home directory
        char *savePtr;
        char *dupString = calloc(2048, sizeof(char));
        if (command->argc == 0){
            char* path = getenv("HOME");
            if (chdir(path) != 0) {
            perror("Error: ");
            }
        // if there are any arguments change directory to the value passed 
        } else {
            strcpy(dupString, command->argv);
            char *token = strtok_r(dupString, " ", &savePtr);

            if (chdir(token) != 0) {
            perror("Error: ");
            } 
        }
        
        promptInput();
        return;

    } else if (strncmp(input, "status", 6) == 0) {
        // if status command was given, call the status function, passing the last forground process pid (lastForProc)
        status(lastForProc);
        promptInput();
        return;

    } else {
        // if the command is not a built in command, pass it to external command processing
        externalCommand(command);
        promptInput();
        return;
    }
}

int main(){
    // main function just runs the input prompt which is endless until exit is called
    promptInput();
    return 0;
}