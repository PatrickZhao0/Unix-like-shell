#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#define COMMAND_MAX 1001

char* absolutePath;
int outputFd = 0;
int inputFd = 0;
int promptPrinted = 0;
typedef struct{
    pid_t pid;
    char* commands;
}Job;

Job suspendedJobs[100];
int jobCount = 0;

void ignHandler();
void prompt(char* path);
char* preprocessPath(char* command);
void pipeImplement(char** pipes, int originalStdin);
void execute(char* commands, int originalStdin);
void tokenize(char* commands, char** commandTokens, const char * restrict delim);
int inputRedirect(char* command, int originalStdin);
void outputRedirect(char* commands, int* redirectErr);
void exterProgram(char** commandTokens);
void cd(char** commandTokens);
void jobs();
void exitCommand(char** commandTokens);
int countOccurence(char target, char* str);


int main(){
    int originalStdin = dup(STDIN_FILENO);
    int originalStdout = dup(STDOUT_FILENO);
    signal(SIGINT, ignHandler); 
    signal(SIGQUIT, ignHandler);
    signal(SIGTSTP, ignHandler);
    signal(SIGSTOP, ignHandler);
    while(1){
        char commands[COMMAND_MAX];
        int RedirectErr = 0;

        dup2(originalStdin, STDIN_FILENO);
        dup2(originalStdout, STDOUT_FILENO);
        outputFd = 0;
        inputFd = 0;
        absolutePath = getcwd(NULL, 0);

        if(promptPrinted == 0) 
            prompt(absolutePath);
        char* status = fgets(commands, COMMAND_MAX-1, stdin);
        if (status == NULL) exit(0);
        promptPrinted = 0;
        char* enter = strstr(commands, "\n");
        enter--;
        if(commands[0] == '|' || commands[0] == '<' || commands[0] == '>'){
            fprintf(stderr, "Error: invalid command\n");
            continue;
        }
        if(*enter == '|' || *enter == '<' || *enter == '>'){
            fprintf(stderr, "Error: invalid command\n");
            continue;
        }
        outputRedirect(commands, &RedirectErr);
        if(RedirectErr == 1){
            continue;
        }
        if(commands[0] == '\n'){
            continue;
        }
        if(countOccurence('|', commands) != 0){
            char* pipes[COMMAND_MAX];
            char* token = strtok(commands, "\n");
            token = strtok(commands, "|");
            int i = 0;
            while(token != NULL){
                pipes[i] = token;
                i++;
                token = strtok(NULL, "|");
            }
            pipes[i] = NULL;
            pipeImplement(pipes, originalStdin);
        }else{
            execute(commands, originalStdin);
        }
    }
    free(absolutePath);
}

void prompt(char* path){
    char* basename = strrchr(path, '/'); //IBM documentation
    if(strcmp(path, basename)) basename++;
    printf("[nyush %s]$ ", basename);
    fflush(stdout);
    promptPrinted = 0;
}

char* preprocessPath(char* command){
    if(command == NULL) return command;
    if(command[0] == '.' && command[1] == '.' && command[2] == '/'){
        return command;
    }
    if(command[0] == '.' || (command[0] != '/' && strchr(command, '/') != NULL)){
        char* processed = malloc((strlen(absolutePath)+strlen(command)+2)*sizeof(char));
        strcpy(processed, absolutePath);
        if (command[0] != '.'){
            strcat(processed,"/");
            strcat(processed, command);
            strcat(processed, "\0");
        }else{
            strcat(processed, ++command);
            command--;
        }
        return processed;
    }else if (access(command, F_OK) != -1){
        return command;
    }else if(command[0] != '/' && strchr(command, '/') == NULL){
        char* processed = malloc((strlen("/usr/bin/") + strlen(command) + 1)*sizeof(char));
        strcpy(processed, "/usr/bin/");
        strcat(processed, command);
        strcat(processed, "\0");
        return processed;
    }
    return command;
}

void tokenize(char* commands, char** commandTokens, const char * restrict delim){
    char* token = strtok(commands, "\n");
    token = strtok(commands, delim);
    unsigned int count = 0;
    while(token != NULL){
        commandTokens[count] = token;
        count++;
        token = strtok(NULL, delim);
    }
    commandTokens[count] = NULL;
}

void exterProgram(char** commandTokens){
    if (*commandTokens == NULL) return;
    char* path = preprocessPath(commandTokens[0]); 
    int pid = fork();
    if (pid == -1)
        fprintf(stderr, "Error: fail to create process\n");
    if (pid == 0){
        execv(path, commandTokens);
        fprintf(stderr, "Error: invalid program\n");
        exit(-1);
    }else{
        wait(NULL);
    }   
}

void cd(char** commandTokens){
    if(*(commandTokens+1) == 0 || *(commandTokens+2) != 0){
        fprintf(stderr, "Error: invalid command\n");
        return;
    }
    if(!chdir(commandTokens[1]))
        return;
    fprintf(stderr, "Error: invalid directory\n");
}

void exitCommand(char** commandTokens){
    if(*(commandTokens+1) != 0){
        fprintf(stderr, "Error: invalid command\n");
        return;
    }
    if(jobCount != 0){
        fprintf(stderr, "Error: there are suspended jobs\n");
        return;
    }
    if(outputFd != 0)
        close(outputFd);
    exit(0);
}

void ignHandler(){
    //printf("\n");
    prompt(absolutePath);
    promptPrinted = 1;
}

int inputRedirect(char* command, int originalStdin){
    char* tokens[2];
    if(countOccurence('<', command) == 0)
        return 0;
    if(countOccurence('<', command) > 1){
        fprintf(stderr, "Error: invalid command\n");
        return 1;
    } 
    char commandCopy[COMMAND_MAX];
    strcpy(commandCopy, command);
    tokenize(commandCopy, tokens, "<");
    tokens[1]++;
    strtok(tokens[1], " ");
    int inputFd = open(tokens[1], O_RDONLY);
    if(inputFd == -1){
        fprintf(stderr, "Error: invalid file\n");
        return 1;
    }
    char* commandArgs[COMMAND_MAX];
    tokenize(tokens[0], commandArgs, " ");
    dup2(inputFd, STDIN_FILENO);
    exterProgram(commandArgs);
    dup2(originalStdin, STDIN_FILENO);
    close(inputFd);
    return 1;
}

void outputRedirect(char* commands, int* redirectErr){
   char* outputPtr = strstr(commands, ">>");
   if(outputPtr != NULL){
        char* outputFile[COMMAND_MAX];
        tokenize(commands, outputFile, ">>");
        *outputPtr = '\0';
        if (countOccurence('>', outputFile[1]) != 0){
            fprintf(stderr, "Error: invalid command\n");
            *redirectErr = 1;
            return;
        }
        outputFile[1]++;
        if(countOccurence('<', outputFile[1]) == 1){
            char* tokens[2];
            char* token = strtok(outputFile[1], " <");
            int i = 0;
            while(token != NULL){
                tokens[i] = token;
                token = strtok(NULL, " <");
                i++;
            }
            inputFd = open(tokens[1], O_RDONLY);
            if(inputFd == -1){
                fprintf(stderr, "Error: invalid file\n");
                *redirectErr = 1;
                return;
            }
            dup2(inputFd, STDIN_FILENO);
        }
        outputFd = open(outputFile[1], O_APPEND | O_CREAT | O_WRONLY, 0777);
        if(outputFd == -1){
            fprintf(stderr, "Error: invalid file\n");
            *redirectErr = 1;
            return;
        }
        dup2(outputFd, STDOUT_FILENO);
        return;
   }
   outputPtr = strstr(commands, ">");
   if(outputPtr != NULL){
        char* outputFile[COMMAND_MAX];
        tokenize(commands, outputFile, ">");
        *outputPtr = '\0';
        if (countOccurence('>', outputFile[1]) != 0){
            fprintf(stderr, "Error: invalid command\n");
            *redirectErr = 1;
            return;
        }
        outputFile[1]++;
        if(countOccurence('<', outputFile[1]) == 1){
            char* tokens[2];
            char* token = strtok(outputFile[1], " <");
            int i = 0;
            while(token != NULL){
                tokens[i] = token;
                token = strtok(NULL, " <");
                i++;
            }
            inputFd = open(tokens[1], O_RDONLY);
            if(inputFd == -1){
                fprintf(stderr, "Error: invalid file\n");
                *redirectErr = 1;
                return;
            }
            dup2(inputFd, STDIN_FILENO);
        }
        outputFd = open(outputFile[1], O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if(outputFd == -1){
            fprintf(stderr, "Error: invalid file\n");
            *redirectErr = 1;
            return;
        }
        dup2(outputFd, STDOUT_FILENO);
        return;
   }
}

int countOccurence(char target, char* str){
    int count = 0;
    for (size_t i = 0; i < strlen(str); i++){
        if (str[i] == target) count++;
    }
    return count;
}

void execute(char* commands, int originalStdin){
    char* commandTokens[COMMAND_MAX];
    char commandsCopy[COMMAND_MAX];
    strcpy(commandsCopy, commands);
        tokenize(commandsCopy, commandTokens, " ");
        if(commandTokens[0] == 0){
            return;
        }
        if(strcmp(commandTokens[0], "cd")  == 0){
            cd(commandTokens);
            return;
        }
        if(strcmp(commandTokens[0], "exit")  == 0){
            exitCommand(commandTokens);
            return;
        }
        if (inputRedirect(commands, originalStdin) == 0)
            exterProgram(commandTokens);
        if (outputFd != 0)
            close(outputFd);
        if (inputFd != 0)
            close(inputFd);
}

void pipeImplement(char** pipes, int originalStdin){ // reference: linix man 
    int pipeFd[2];
    pipe(pipeFd);
    switch (fork()) {
        case -1: 
            break;
        case 0:
            close(pipeFd[0]);                       
            dup2(pipeFd[1], STDOUT_FILENO);
            execute(pipes[0], originalStdin);
            close(pipeFd[1]);                      
            exit(0);
        default:
            close(pipeFd[1]);                       
            dup2(pipeFd[0], STDIN_FILENO);
            if(pipes[2] == NULL){
                execute(pipes[1], originalStdin);
            }else{
                pipes++;
                pipeImplement(pipes, originalStdin);
            }
            close(pipeFd[0]);
            wait(NULL);                      
            return;
    }
}

void jobs(){
    if(jobCount == 0) 
        return;
    
    // for(int i = 0; suspendedJobs[i] != NULL){
    // }
}