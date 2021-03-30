#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

#define MAX_LINE 80
#define DELIMITERS " \t\n\v\f\r"

void show_history(char * hist[][MAX_LINE + 1]) {
    for (int i = 9; i >=0; i--){
		if (strcmp(hist[i]," ")!=0) {
            printf("%d %s",i,hist[i]);  
        }
	}
}

void init_history(char * hist[][MAX_LINE + 1]) {
    for (int i = 0; i < 10; ++i) {
        strcpy(hist[i]," ");
    }
}

void keep_hist(char * hist[][MAX_LINE + 1]) { // only keep 10 history command
    for (int i = 9; i > 0; --i) {
        strcpy(hist[i],hist[i-1]);
    }
}

void init_args(char * args[]) {
    for (size_t i = 0; i != MAX_LINE / 2 + 1; ++i) {
        args[i] = NULL;
    }
}

void init_command(char * command) {
    strcpy(command, "");
}

void refresh_args(char * args[]) {
    while ( * args) {
        free( * args); // to avoid memory leaks
        * args++ = NULL;
    }
}

size_t parse_input(char * args[], char * original_command) {
    size_t num = 0;
    char command[MAX_LINE + 1];
    strcpy(command, original_command); // make a copy
    char * token = strtok(command, DELIMITERS);
    
    while (token != NULL) {
        printf("======= Debug: args[%ld] = %s ======= \n", num, token);
        
        args[num] = malloc(strlen(token) + 1);
       
        strcpy(args[num], token);
        ++num;
        token = strtok(NULL, DELIMITERS);
    }
    return num;
}

int get_input(char * command,char * hist[][MAX_LINE + 1]) {
    char input_buffer[MAX_LINE + 1];
    if (fgets(input_buffer, MAX_LINE + 1, stdin) == NULL) {
        fprintf(stderr, "Failed to read input!\n");
        return 0;
    }
   
    if (strcmp(input_buffer, "!!") == 0) { // for !!
        if (strlen(command) == 0) { // no history yet
            fprintf(stderr, "No command in history. \n");
            return 0;
        }
        printf("%s", command); // keep the command unchanged and print it
        return 1;
    }

    if (strncmp(input_buffer, "!", 1) == 0) {  //for !(number)
            
            //delete '!' then get the number after '!'
            char *p = input_buffer;
            char *q = input_buffer;
            while(*q){
                if (*q !='!') *p++ = *q;
                q++;
            }

            int hisNum = atoi(input_buffer); 
            if(hisNum < 0 || hisNum>9 || strcmp(hist[hisNum]," ")==0){
                printf("%s\n","No such command in history");
                return 0;
            }
            strcpy(command, hist[hisNum]);
            printf("history %d: %s \n",hisNum, command); // keep the command unchanged and print it
            return 1;
    }

    keep_hist(hist);               // move a space to store new command history
	strcpy (hist[0],input_buffer); // update the history
    strcpy(command, input_buffer); // update the command
    return 1;
}

int check_ampersand(char ** args, size_t * size) {
    size_t len = strlen(args[ * size - 1]);
    if (args[ * size - 1][len - 1] != '&') {
        return 0;
    }
    if (len == 1) { // remove this argument if it only contains '&'
        free(args[ * size - 1]);
        args[ * size - 1] = NULL;
        --( * size); // reduce its size
    } else {
        args[ * size - 1][len - 1] = '\0';
    }
    return 1;
}

unsigned check_redirection(char ** args, size_t * size, char ** input_file, char ** output_file) {
    unsigned flag = 0;
    size_t to_remove[4], remove_cnt = 0;
    for (size_t i = 0; i != * size; ++i) {
        if (remove_cnt >= 4) {
            break;
        }
        if (strcmp("<", args[i]) == 0) { // input
            to_remove[remove_cnt++] = i;
            if (i == ( * size) - 1) {
                fprintf(stderr, "No input file provided!\n");
                break;
            }
            flag |= 1;
            * input_file = args[i + 1];
            to_remove[remove_cnt++] = ++i;
        } else if (strcmp(">", args[i]) == 0) { // output
            to_remove[remove_cnt++] = i;
            if (i == ( * size) - 1) {
                fprintf(stderr, "No output file provided!\n");
                break;
            }
            flag |= 2;
            * output_file = args[i + 1];
            to_remove[remove_cnt++] = ++i;
        }
    }
    /* Remove I/O indicators and filenames from arguments */
    for (int i = remove_cnt - 1; i >= 0; --i) {
        size_t pos = to_remove[i]; // the index of arg to remove
        // printf("%lu %s\n", pos, args[pos]);
        while (pos != * size) {
            args[pos] = args[pos + 1];
            ++pos;
        }
        --( * size);
    }
    return flag;
}

int redirect_io(unsigned io_flag, char * input_file, char * output_file, int * input_desc, int * output_desc) {
    // printf("IO flag: %u\n", io_flag);
    if (io_flag & 2) { // redirecting output
        * output_desc = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 644);
        if ( * output_desc < 0) {
            fprintf(stderr, "Failed to open the output file: %s\n", output_file);
            return 0;
        }
        // printf("Output To: %s %d\n", output_file, *output_desc);
        dup2( * output_desc, STDOUT_FILENO);
    }
    if (io_flag & 1) { // redirecting input
        * input_desc = open(input_file, O_RDONLY, 0644);
        if ( * input_desc < 0) {
            fprintf(stderr, "Failed to open the input file: %s\n", input_file);
            return 0;
        }
        // printf("Input from: %s %d\n", input_file, *input_desc);
        dup2( * input_desc, STDIN_FILENO);
    }
    return 1;
}

void close_file(unsigned io_flag, int input_desc, int output_desc) {
    if (io_flag & 2) {
        close(output_desc);
    }
    if (io_flag & 1) {
        close(input_desc);
    }
}

void detect_pipe(char ** args, size_t * args_num, char ** * args2, size_t * args_num2) {
    for (size_t i = 0; i != * args_num; ++i) {
        if (strcmp(args[i], "|") == 0) {
            free(args[i]);
            args[i] = NULL;
            * args_num2 = * args_num - i - 1;
            * args_num = i;
            * args2 = args + i + 1;
            break;
        }
    }
}

int run_command(char ** args, size_t args_num) {
    /* Detect '&' to determine whether to run concurrently */
    int run_concurrently = check_ampersand(args, & args_num);
    /* Detect pipe */
    char ** args2;
    size_t args_num2 = 0;
    detect_pipe(args, & args_num, & args2, & args_num2);
    /* Create a child process and execute the command */
    pid_t pid = fork();
    if (pid < 0) { // fork failed
        fprintf(stderr, "Failed to fork!\n");
        return 0;
    } else if (pid == 0) { // child process
        if (args_num2 != 0) { // pipe
            /* Create pipe */
            int fd[2];
            pipe(fd);
            /* Fork into another two processes */
            pid_t pid2 = fork();
            if (pid2 > 0) { // child process for the second command
                /* Redirect I/O */
                char * input_file, * output_file;
                int input_desc, output_desc;
                unsigned io_flag = check_redirection(args2, & args_num2, & input_file, & output_file); // bit 1 for output, bit 0 for input
                io_flag &= 2; // disable input redirection
                if (redirect_io(io_flag, input_file, output_file, & input_desc, & output_desc) == 0) {
                    return 0;
                }
                close(fd[1]);
                dup2(fd[0], STDIN_FILENO);
                wait(NULL); // wait for the first command to finish
                execvp(args2[0], args2);
                close_file(io_flag, input_desc, output_desc);
                close(fd[0]);
                fflush(stdin);
            } else if (pid2 == 0) { // grandchild process for the first command
                /* Redirect I/O */
                char * input_file, * output_file;
                int input_desc, output_desc;
                unsigned io_flag = check_redirection(args, & args_num, & input_file, & output_file); // bit 1 for output, bit 0 for input
                io_flag &= 1; // disable output redirection
                if (redirect_io(io_flag, input_file, output_file, & input_desc, & output_desc) == 0) {
                    return 0;
                }
                close(fd[0]);
                dup2(fd[1], STDOUT_FILENO);
                execvp(args[0], args);
                close_file(io_flag, input_desc, output_desc);
                close(fd[1]);
                fflush(stdin);
            }
        } else { // no pipe
            /* Redirect I/O */
            char * input_file, * output_file;
            int input_desc, output_desc;
            unsigned io_flag = check_redirection(args, & args_num, & input_file, & output_file); // bit 1 for output, bit 0 for input
            if (redirect_io(io_flag, input_file, output_file, & input_desc, & output_desc) == 0) {
                return 0;
            }
            execvp(args[0], args);
            close_file(io_flag, input_desc, output_desc);
            fflush(stdin);
        }
    } else { // parent process
        if (!run_concurrently) { // parent and child run concurrently
            wait(NULL);
        }
    }
    return 1;
}

int main(void) {
    int should_run =1;
    char * args[MAX_LINE / 2 + 1]; /* command line (of 80) has max of 40 arguments */
    char command[MAX_LINE + 1];
    char * hist[10][MAX_LINE + 1];
    init_args(args);
    init_command(command);
    
    init_history(hist);

    while (should_run) {
        
        fflush(stdout);
        fflush(stdin);
        printf("osh>");
        /* Make args empty before parsing */
        refresh_args(args);
        /* Get input and parse it */
        if (!get_input(command,hist)) {
            continue;
        }
        size_t args_num = parse_input(args, command);
        /* Continue or exit */
        if (args_num == 0) { // empty input
            printf("Please enter the command! (or type \"exit\" to exit)\n");
            continue;
        }
        if (strcmp(args[0], "exit") == 0 && args_num == 1) {
            should_run=0;
            break;
        }
        else if (strcmp(args[0], "history") == 0 && args_num == 1) {
            show_history(hist);
        }
       
        /* Run command */
        run_command(args, args_num);
    }
    refresh_args(args); // to avoid memory leaks!
    return 0;
}