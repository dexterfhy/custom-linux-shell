#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <math.h>

#include "token.h"
#include "command.h"

#define MAX_LENGTH_INPUT 100*1000*1000 //100 commands, 1000 arguments, 1000 char for each
#define MAX_LENGTH_PATH 1000

typedef struct sigaction sig;

//stores information about each process which is tracked by program using the Proc* running array
typedef struct Process {
	int pid;
	char* job;
	char separator;
	char status;
} Proc;

/*-------GENERAL VARIABLES/FUNCTIONS-------*/
char* input; //stores initial user input
char* prompt; //displayed to user as part of shell
char currentDir[MAX_LENGTH_PATH], homeDir[MAX_LENGTH_PATH], bufUser[MAX_LENGTH_PATH], bufHost[MAX_LENGTH_PATH];
//stores current directory, home directory, a buffer for username, and a buffer for host name
char** tokens; 
pid_t parentPID; //used to validate pid of process when a signal is caught 
Command* firstCmd; //pointer to the first command in the linked list of Commands

void processInput(Command** first); //processes each Command in user input based on the starting command
void freeResources(); //frees all memory that was dynamically allocated during execution
void printHelp();
/*-----------------------------------------*/


/*---------------JOB CONTROL---------------*/
int foreground = 0; //tracks whether the main shell is foreground process (0) or not (pid of foreground child)
Proc* running; //stores the job information of all process currently running 

int pgCnt = 0; //stores the number of PIDs in the array childPG
int quit = 0; //flag for whether to quit the processInput() method - it's set to true when a SIGINT, SIGQUIT, SIGTSTP signal is received

void registerSignalHandler(); //used to register the signal handler to the process at the start
void catchSignals(int signo); //signal handler method
void addToPG(int pid, Command* cmd); //adds a child PGID to the childPG array
void removeFromPG(int pid); //removes a child PGID from the childPG array
void clearPG(); //resets the running array
void changeStatus(int childPID, char status); //changes the status of the child if it stopped/resumed/or got killed
/*-----------------------------------------*/

int main(){
	printf("\n\nWelcome!\n");
	printHelp(); //prints user guide (list of builtin commands)
	printf("\n\n");
	
	//set initial values like main parent process ID, home directory, user name and host name
	// + register signal handler 
	parentPID = getpid();
	struct passwd* pw = getpwuid(getuid());
	strcpy(homeDir, pw->pw_dir);
	getlogin_r(bufUser, 1000);
	gethostname(bufHost, 1000);
	
	//allocate memory for the running jobs array
	running = (Proc*) malloc(sizeof(Proc) * MAX_NUM_TOKENS);
	if (running == NULL) {printf("Failure to allocate memory.\n"); exit(1);}
	
	prompt = NULL;
	registerSignalHandler();

	while (1){
		//PROBLEM IS BETWEEN HERE AND MALLOC STATEMENTS
		//dynamically allocate memory and initialize values
		input = malloc(sizeof(char) * MAX_LENGTH_INPUT);
		tokens = malloc(sizeof(char*) * MAX_NUM_TOKENS);
		firstCmd = malloc(sizeof(Command));	
		if (input == NULL || tokens == NULL || firstCmd == NULL) {printf("Failure to allocate memory.\n"); exit(1);}
		
		initializeCommand(firstCmd);	
		getcwd(currentDir, MAX_LENGTH_PATH);

		//while loop that prompts user for input until valid input is received
		while (1){	
			//if no prompt was specified, print home directory
			if (prompt == NULL){ 
				//if the first occurence (if any) of the home directory string is found at the start of the current directory
				//replace the occurence with ~, just like the terminal
				if (strstr(currentDir, homeDir) == currentDir) {
					char temp[MAX_LENGTH_PATH]; 
					strcpy(temp, currentDir); //set temp to currentDir
					strcpy(temp, &temp[strlen(homeDir)]); //replace temp with a string that starts AFTER the home directory substring

					strcpy(currentDir, "~"); //set currentDir to ~ and then concatenate temp
					strcat(currentDir,temp);	
				}

				printf("%s@%s:%s$ ", bufUser, bufHost, currentDir);
			} else {
				printf("%s ", prompt);
			}
	
			fgets(input, MAX_LENGTH_INPUT, stdin);
	
			//checks that input is valid (no interruption occured)
			if (strlen(input) == 0 && errno == EINTR){
				printf("\n");
				continue;
			} else if (strlen(input) == 0) {
				continue;
			} else if (input[0] == '\n'){
				printf("Nothing was entered.\n");
				continue;
			} 
			break;
		}

		//if the last command does not have a separator (i.e. &) then add a sequential separator
		int foundSep = 0;
		//go from the back, find the first non-space character 
		//if its a separator, then good, else add the sequential separator
		for (int k = strlen(input)-2; k>=0; k--){
		//input[strlen(input)] is \0
		//input[strlen(input)-1] is \n
		//input[strlen(input)-2] is the last char of actual input
			if (input[k] != ' '){
				//this condition tests that the char found is a separator, is not the first char of the string
				//and is preceded by a space, so 'ps;' wouldn't work
				if (isSeparator(input[k]) == 1 && k>0 && input[k-1] == ' '){
					foundSep = 1;
				} else {
				}
				break;
			}
		}
		if (foundSep == 0){ //no separator found from the back
			input[strlen(input)-1] = ' '; //replace new line character with space			
			strcat(input, ";");
		}

		int result = tokenise(input, tokens);
		
		//check the resulting token array, and process tokens only when array is valid
		if (result == -1){
			printf("Error - Too many tokens!\n");		
		} else if (result == 0){
			printf("No input detected!\n");	
		} else {
			int noCommands = separateCommands(tokens, firstCmd);
			if (noCommands == -1) {
				perror("Error separating commands from input.\n");
			} else {	
				processInput(&firstCmd);
			}
			
			//memory was dynamically allocated to store global pointers
			//this releases the memory before termination
			freeResources();

		}
	}
	exit(0);
}

void processInput(Command** first){
	Command** current = first;
	quit = 0;
	int fdPipe[2];
	
	//run through each Command and process them
	while (*current){
		//IF ELSE block that checks for each of the four built in commands that must run on the main process
		if (strcmp((*current)->path, "helpme") == 0) {
			printHelp();
		} else if (strcmp((*current)->path, "exit") == 0) {
			//kills all running processes
			if (waitpid(-1, NULL, WNOHANG) == 0) {
				printf("\nThese child processes were killed while terminating the shell:\n");
				for (int i=0; i<pgCnt; i++){
					printf("[%d] %d - %s\n", i+1, running[i].pid, running[i].job);
					kill(-1 * running[i].pid, SIGKILL);
				}				
				clearPG();
			} 
			exit(0);
		} else if (strcmp((*current)->path, "cd") == 0){
			//replace home directory string with tilde if possible
			//change directory to path argument
			if ((*current)->argc == 1 || strcmp((*current)->argv[1], "~") == 0){
				chdir(homeDir);
			} else {
				if (chdir((*current)->argv[1]) == -1) printf("Path not recognized.\n");
			}	
		} else if (strcmp((*current)->path, "prompt") == 0){
			//free previous value of prompt
			free(prompt);			
			//allows user to reset shell prompt to the standard if prompt was entered as command with no arguments
			if ((*current)->argc  == 1){ 
				prompt = NULL;
			} else {
				printf("(Prompt changed. Reset by entering 'prompt' with no arguments.)\n");
				prompt = strdup((*current)->argv[1]);
				//assign a new pointer to prompt, a duplicate of the current argv value
			}
		} else if (strcmp((*current)->path, "pwd") == 0){
			//get current working directory with getcwd, and then print it
			char dirPrint[MAX_LENGTH_PATH];
			if (getcwd(dirPrint, MAX_LENGTH_PATH) == NULL){
				printf("Error printing current working directory.\n");
			} else {
				printf("%s\n", dirPrint);
			}			
		} else if (strcmp((*current)->path, "jobs") == 0) { ///--- new	
			//print out values stored in running array
			if (pgCnt == 0) {
				printf("No jobs exist.\n");
			} else {			
				for (int m = 0; m < pgCnt ; m++){
					if (running[m].status == 'R') {
						printf("[%d]   Running\t\t%d - %s\n", m+1, running[m].pid, running[m].job);
					} else {
						printf("[%d]   Stopped\t\t%d - %s\n", m+1, running[m].pid, running[m].job);
					}
				}
			}
		} else if (strcmp((*current)->path, "fg") == 0){ ///--- new
			if ((*current)->argc  == 1){ 
				printf("No job id specified.\n");
			} else {
			//check that its valid int
			int jobID = 0;

			//loops through each character in the first argument from the back to the front
			//first, the character is converted to an int
			//then it uses pow from the math library to exponentiate the value based on its place
			//then adds that value to the final int jobID variable
			for (int i=strlen((*current)->argv[1])-1; i>=0; i--){
				int charToInt = (int) ((*current)->argv[1][i] - '0');
				jobID += charToInt * (int) pow(10, strlen((*current)->argv[1])-1-i);
			}
				if (jobID <= 0 || jobID > pgCnt){
					printf("Invalid job id specified.\n");
				} else {
					int status = 0; 

					//extract the child process number
					int childProcess =  running[jobID-1].pid;
					printf("%s\n", running[jobID-1].job);
					kill(childProcess, SIGCONT); //send a continue signal to process
					//if it's already running, it will be ignored

					//WNOHANG with wait to just check if it continued
					waitpid(childProcess, &status, WCONTINUED | WNOHANG);
					//here status is checkd to see if it continued
					if (WIFCONTINUED(status)){
						//if continued, then reap that status and wait for it
						//to chagne state i.e. continue
						waitid(P_PID, childProcess, NULL, WCONTINUED);
					}
					
					//set child as foreground process
					status=0;//reset status information
					tcsetpgrp(STDIN_FILENO, childProcess);
					tcsetpgrp(STDOUT_FILENO, childProcess);	
					foreground = childProcess;

					//wait for child to terminate
					while (waitpid(childProcess, &status, 0) > 0) {}

					if (WIFEXITED(status) == 0) quit = 1;

					//set parent back as foreground process (main shell)
					tcsetpgrp(STDIN_FILENO, parentPID);
					tcsetpgrp(STDOUT_FILENO, parentPID);
					foreground = 0;
				}
			}
		} else {
			//fork to process other commands in the child
			pipe(fdPipe);	
			pid_t pid = fork();

			//permit effective job control by setting the child process to be in its own process group
			//to avoid race conditions, both parent and child will set the pgid of the child to be pid of child
			if (pid == 0){
				setpgid(0, getpid());
				//set the pgid for all child processes for this string of commands to the pid of the first child
			} else {
				setpgid(pid, pid);
				addToPG(pid, *current); 
				if ((*current)->separator == '&') printf("[%d] %d - %s\n", pgCnt, pid, running[pgCnt-1].job);
				//parent	
				//pid here refers to the value returned by the fork which is the child pid
			}

			switch ((*current)->separator){
				case '&':
					if (pid==0){ //child, execute
						executeCommand(*current);
					} else if (pid<0) { //error
						printf("Error executing command.\n");
					}
					break;
				case ';':
					//similarly, to avoid race conditions, both parent and child will set the child as the foreground process
					//for sequential execution
					//and when the parent returns after waiting for child to terminate, it sets itself as the foreground
					//it has already set SIG_IGN on SIGTTIN and SIGTTOU so it is able to reclaim itself as
					//the foreground process group once the child terminates
					if (pid>0) { //parent waits
						//set child process as foreground process
						tcsetpgrp(STDIN_FILENO, pid);
						tcsetpgrp(STDOUT_FILENO, pid);	
						foreground = pid;
		
						int status;
						//wait till pid child dies and no longer exists
						while (waitpid(pid, &status, 0) > 0){					
						}

						//if a foreground process was terminated 
						//then set quit flag to 1 so that the rest of the commands are ignored
						//WIFEXITED is used to check if the process terminated normally or using a user signal
						//such as CTRL C \ etc
						if (WIFEXITED(status) == 0) quit = 1;

						//once child process has ended, set main shell process as foreground process again
						tcsetpgrp(STDIN_FILENO, parentPID);
						tcsetpgrp(STDOUT_FILENO, parentPID);
						foreground = 0;

						//following code waits for child to finish output to stdout
						close(fdPipe[1]); //close write end
						
						char buf[1];
						while (read(fdPipe[0], buf, 1) > 0){
							//do nothing until child exits (closing all fds and stdout)
						}

					} else if (pid==0){ //child execute
						//set child as foreground process
						tcsetpgrp(STDIN_FILENO, getpid());
						tcsetpgrp(STDOUT_FILENO, getpid());
						foreground = getpid();

						close(fdPipe[1]); //close write end
						close(fdPipe[0]); //close read end

						executeCommand(*current);
					} else {
						printf("Error executing command.\n");
					}	
					break;
				case '|':
					if (pid==0){
						tcsetpgrp(STDIN_FILENO, getpid());
						tcsetpgrp(STDOUT_FILENO, getpid());
						foreground = getpid();

						pipeCommands(current, STDIN_FILENO);
					} else {
						//set child process as foreground process
						tcsetpgrp(STDIN_FILENO, pid);
						tcsetpgrp(STDOUT_FILENO, pid);	
						foreground = pid;
		
						int status;
						waitpid(pid, &status, 0);

						//if a foreground process was terminated 
						//then set quit flag to 1 so that the rest of the commands are ignored
						//WIFEXITED is used to check if the process terminated normally or using a user signal
						//such as CTRL C \ etc
						if (WIFEXITED(status) == 0) quit = 1;

						//once child process has ended, set main shell process as foreground process again
						tcsetpgrp(STDIN_FILENO, parentPID);
						tcsetpgrp(STDOUT_FILENO, parentPID);
						foreground = 0;
						
						//accelerate the value of the current command forward to sync with the recursive call
						//bring current to the last command BEFORE a command without a pipe separator or BEFORE a NULL command
						while ((*current)->nextCmd != NULL && (*current)->separator == '|'){
						//short circuit evaluation checks that the next cmd isn't NULL first before trying to access the separator
							current = &((*current)->nextCmd);
						}
					}
			}
		}
		current = &((*current)->nextCmd);

		if (quit == 1) break;
	}
}

void freeResources(){
	//free all resources here
	
	free(input);
	input = NULL;
	
	//tokens will be freed by free-ing commands, which contain malloc-ed pointers to the same block
	//pointers which were globbed were freed already as the argv array was built
	tokens = NULL;
	
	Command** current = &firstCmd;
	Command** del;
	//iterate through each Command and free resources
	while (*current){
		del = current; //del holds a pointer to the current command
		current = &((*current)->nextCmd); //main pointer to current command is moved forward to next command
		//below, del (which still points to previous command) is freed by dereferencing &

		//given that each Command is dynamically allocated
		//and each Command has an char** argv which is dynamically allocated based on the number of arguments
		//and each char** argv has a char* that is dynamically allocated based on the strlen of each argument
		//free is called on each of these three instances of allocation
		for (int i = 0; i<(*del)->argc; i++){
			free((*del)->argv[i]); //free each individual argument
		}
		free((*del)->argv); //free the block of arguments
		free(*del); //free the command itself
		*del = NULL;
	}
	
	firstCmd = NULL;
}

void registerSignalHandler(){
	//register custom handler
	sig signal;
	signal.sa_flags = 0;
	signal.sa_handler = catchSignals;

	sigfillset(&(signal.sa_mask));
	//block all other signals including the three being handled
	//the mask will be restored after handler so not to worry

	//set signals to respond to
	if (sigaction(SIGINT, &signal, NULL) != 0 
		|| sigaction(SIGQUIT, &signal, NULL) != 0
		|| sigaction(SIGTSTP, &signal, NULL) != 0
		|| sigaction(SIGCHLD, &signal, NULL) != 0
		|| sigaction(SIGTERM, &signal, NULL) != 0){
		printf("Error registering sigaction!\n");
		exit(1);
	}

	//register handler to ignore SIGTTIN and SIGTTOU so that 
	//main shell process can set its PGID to be the foreground process once the child process terminates 
	sig sigign;
	sigign.sa_flags = 0;
	sigign.sa_handler = SIG_IGN;
	sigfillset(&(sigign.sa_mask));
	if (sigaction(SIGTTIN, &sigign, NULL) != 0
		|| sigaction(SIGTTOU, &sigign, NULL) != 0){
		printf("Error registering sigaction!\n");
		exit(1);
	}
}


void catchSignals(int signo){
	//process the three user interruption signals from terminal keyboard
	if (signo == SIGINT || signo == SIGQUIT || signo == SIGTSTP){
		if (getpid() == parentPID){ //parent waits for child to terminate
			if (foreground == 0){
				//waitpid WNOHANG returns -1 if no children exist 
				if (waitpid(-1, NULL, WNOHANG) <= 0) {
					printf("\nUse 'exit' to close the shell instead.");			
				} else {
					//wait returns -1 only if there are no more children to wait for
					while (waitpid(-1, NULL, WNOHANG) > 0){	
					}
				} //parent waits for all children to finish
			}
		} else {
			//child will terminate itself
			exit(0);
		}
	//claim zombies here, or change status of current processes
	} else if (signo == SIGCHLD){
		//for each currently running process that is known (using the running array)
		//use waitid to see if they have changed state
		for (int i=0; i<pgCnt; i++){
			siginfo_t status;
			status.si_pid = 0;
			//waitid stores additional status information in 'status'
			
			//get status information, and if si_pid = 0, it means that there is no process with a waitable state, 
			//so break the for loop and stop searching
			//else compare the si_code to see how it ended
			while (waitid(P_ALL, 0, &status, WEXITED | WCONTINUED | WSTOPPED | WNOHANG) >= 0){
					if (status.si_pid == 0) {
						break;
						//test if child was resumed or stopped
					} else if (status.si_code == CLD_CONTINUED){
						changeStatus(status.si_pid, 'C');
					} else if (status.si_code == CLD_STOPPED){
						//print that the process was stopped
						for (int i=0; i<pgCnt; i++){
							if (running[i].pid == status.si_pid){
								printf("\n[%d]+ Stopped\t\t%d - %s\n", i+1, running[i].pid, running[i].job);
							}
						}
						changeStatus(status.si_pid, 'S');
					} else if (status.si_code == CLD_EXITED || status.si_code == CLD_KILLED){
						//check if ended process was a background one, if so, print the job id and indicate that it ended
						for (int i=0; i<pgCnt; i++){
							if (running[i].pid == status.si_pid){
								if (running[i].separator == '&') printf("\n[%d]- Done\t\t%d - %s", i+1, running[i].pid, running[i].job);
							} 
						}
					}
			}
			
		}	
	} else if (signo == SIGTERM){
		if (parentPID == getpid()) {
			printf("Shell process cannot be terminated with 'kill'. Use 'exit' instead.\n");
		} else {
			exit(0);
		}
	}
	
	//clear process group array by sending a signal 0 with kill
	//kill with 0 sends no signal but performs error checking anyway
	for (int i=pgCnt-1; i>=0; i--){ //iterate from the back as elements are pushed forward once removed
		if (kill(running[i].pid, 0) != 0){
			//process doesn't exist anymore
			removeFromPG(running[i].pid);
		}
	}
}

void addToPG(int pid, Command* cmd){
	//if a match for the process group is already found in array, then ignore and return back
	//this happens when both parent and child tries to add it
	for (int i=0; i<pgCnt; i++){
		if (running[i].pid == pid) return;
	}
	
	//add the values of the pid and command to the running array element
	running[pgCnt].pid = pid;

	int loop = 0, lengthCmd = 0;
	
	//reset all values of job
	free(running[pgCnt].job);	
	running[pgCnt].job = NULL;
	running[pgCnt].job = (char*) malloc(sizeof(char) * MAX_NUM_TOKENS);
	//fill with null-terminating characters to write over previous values if any
	for (int j = 0; j< MAX_NUM_TOKENS; j++){
		running[pgCnt].job[j] = '\0';
	}
	
	//after memory is assigned, copy over the value of each argument to the running array element
	while (cmd->argv[loop] != NULL){
		for (int j=0; j<strlen(cmd->argv[loop]); j++){		
			running[pgCnt].job[lengthCmd] = cmd->argv[loop][j];
			lengthCmd++;
		}
		running[pgCnt].job[lengthCmd] = ' '; lengthCmd++;
		loop++;
	}
	
	//assign separator and status
	running[pgCnt].separator = cmd->separator;
	running[pgCnt].status = 'R';
	pgCnt++;
}

void removeFromPG(int pid){
	//find the first occurence of pid, then iterate through remaining elements and bring them forward
	for (int i=0; i<pgCnt; i++){
		if (running[i].pid == pid){
			free(running[i].job);
			//free existing char array

			//move each subsequent element after the deleted one forwards
			for (int j=i; j<pgCnt-1; j++){
				running[j].pid = running[j+1].pid;
				running[j].job = running[j+1].job;
				running[j].separator = running[j+1].separator;
				running[j].status = running[j+1].status;
			}

			//assign last element a nullptr
			running[pgCnt-1].job = NULL;
			break;
		}
	}
	pgCnt--;
}

void clearPG(){
	//free each element in running.job
	for (int i=0; i<pgCnt; i++){
		free(running[i].job);
	}
	pgCnt = 0;
	free(running);
}

void changeStatus(int childPID, char status){
	int pos = -1; //tries to find the positino of the process that got continued/stopped in the running array
	for (int i=0; i<pgCnt; i++){
		if (running[i].pid == childPID){
			pos = i;
			break;
		}
	}
	
	//if found, set the status char to be either R for running or S for stopped
	if (pos == -1){
		return;
	} else {
		if (status == 'C'){
			running[pos].status = 'R';
		} else if (status == 'S') {
			running[pos].status = 'S';
		}		
	}

}

void printHelp(){
	printf("************************BUILT-IN COMMANDS************************\n");
	printf("COMMAND\t\tDESCRIPTION\n");
	printf("prompt <s>\tChanges the terminal prompt to <s>. To reset, enter the command without any arguments.\n");
	printf("pwd\t\tPrints the current working directory.\n");
	printf("cd <s>\t\tChanges the current working directory to <s>. Accepts the use of wildcards.\n");
	printf("jobs\t\tPrints out the list of currently running processes, along with their status. Does not accept arguments.\n");
	printf("fg <d>\t\tSets the process whose index matches <d> to run as the foreground process.\n");
	printf("exit\t\tTerminates the shell.\n");
	printf("helpme\t\tPrint this guide again.\n");
	printf("*****************************************************************\n");
}