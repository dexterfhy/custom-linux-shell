#include "command.h"

//returns number of commands, or -1 if error
int separateCommands(char* tokens[], Command* first){
	int idx = 0, commandCount = 0, commandStart = 0, commandEnd = 0;
	Command** current = &first;
	
	while (tokens[idx] != NULL){
		//ERRORNEOUS USER INPUT HANDLING
		if (isSeparator(tokens[idx][0]) == 1){
			//error conditions (separator is first token, or precedes/follows another token)
			//short circuit evaluation prevents is Separator(tokens]idx-1]) from 
			//accessing a negative index if idx is 0
			if (idx == 0 || isSeparator(tokens[idx-1][0]) == 1) return -1;
			
			if (commandCount>0){
					(*current)->nextCmd = malloc(sizeof(Command));
					current = &((*current)->nextCmd);
			} //set previous command to point to new command, then move current forward

			commandEnd = idx-1; 

			//set path i.e first token into command
			(*current)->path = tokens[commandStart];
			
			//set separator as first character of tokens[idx]
			(*current)->separator = tokens[idx][0];
			
			(*current)->nextCmd = NULL;

			searchRedirection(tokens, (*current), commandStart, commandEnd);
			buildCommandArgumentArray(tokens, (*current), commandStart, commandEnd);
			
			//increment command count for accessing command array
			commandCount++;
			//increment start of next command token location to after separator
			commandStart = idx+1;
		}
		idx++;
	}
	
	return commandCount;
}

//return whether a given char is one of three separators
int isSeparator(char token){
	if (token == '|' || token == '&' || token == ';') return 1;
	return 0;
}

//search a command for the presence of a redirection symbol < or >
void searchRedirection(char *token[], Command *cp, int first, int last){
	//set both stdin_file and stdout_file to null first
	cp->stdin_file = cp->stdout_file = NULL;

	//check if theres more than one argument
	if (first != last){
		//if the symbols are encountered, add the argument after the symbol to stdin or stdout_file
		for (int i=first+1; i<=last; i++){
			if (strcmp(token[i],"<") == 0 && i != last) cp->stdin_file = token[i+1];
			if (strcmp(token[i],">") == 0 && i != last) cp->stdout_file = token[i+1];
		}
	}
}

void buildCommandArgumentArray(char *token[], Command *cp, int first, int last){
	char* arguments[MAX_NUMBER_ARGUMENTS];
	glob_t temp;
	int noArguments = 0, res;

	//copy first token (path) into arguments as path
	arguments[noArguments] = token[first];
	noArguments++;
		
	//go through each token, check for redirection (skip them)
	//also check if token has wildcards with glob (if so, iterate and add to arguments array
	for (int i = first+1; i<=last; i++){
		if (strcmp(token[i], "<") == 0 || strcmp(token[i], ">") == 0){
			i++; //skip the redirection symbol and its location
		} else {
			//first check if a wildcard character exists in the token
			if (strchr(token[i], '*') != NULL || strchr(token[i], '?') != NULL || strchr(token[i], '[') != NULL){
				//if it exists, that means we glob it
				//dynamically create a separate char* string so that token[i] isn't erased
				char* tempTok = strdup(token[i]);

				//glob stores result in temp struct, which has temp.argc (number of paths) and temp.argv (array of path names)
				res = glob(tempTok, GLOB_TILDE, NULL, &temp);
				if (res == 0){
					free(token[i]); 
					//no longer used, so free it as it won't be freed later in freeResources via freeing each command
					for (int j = 0; j < temp.gl_pathc; j++){
						arguments[noArguments] = temp.gl_pathv[j];
						noArguments++;
					} //copying over each valid path into arguments
				} else if (res == GLOB_NOMATCH) { //if there's no valid path matched, then copy the same token over
					arguments[noArguments] = token[i];
					noArguments++;
				} //additional error handling
				free(tempTok); //free the tempTok
			} else {
				arguments[noArguments] = token[i];
				noArguments++;
			} //additional error handling
		}		
	}
	cp->argc = noArguments;

	//malloc argv of command to the size of actual arguments found	
	cp->argv = (char**) malloc((sizeof(char*) * noArguments)+1); //additional 1 space for the NULL pointer as the last argument
	if (cp->argv == NULL) {
		perror("Failure to assign memory for arguments using realloc.\n");
		exit(1);
	}

	//copy over arguments
	for(int k = 0; k < noArguments; k++){
			cp->argv[k] = strdup(arguments[k]);
	}
	cp->argv[noArguments] = NULL;

	globfree(&temp); //free the glob structure
}

//set all members of cp to empty/null values
void initializeCommand(Command* cp){
	cp->path = NULL;
	cp->separator = '\0';
	cp->argc = 0;
	cp->argv = NULL;
	cp->stdin_file = NULL;
	cp->stdout_file = NULL;
	cp->nextCmd = NULL;
}


void executeCommand(Command* cp){

	//FILE REDIRECTION HANDLING
	if (cp->stdin_file != NULL){ 
		int fdIn;
		if ((fdIn = open(cp->stdin_file, O_RDONLY, 0664)) == -1){ //rw r r permissions
			printf("Error opening file.\n");
			exit(1);		
		};
		dup2( fdIn, STDIN_FILENO);
		close(fdIn);
	}	
	if (cp->stdout_file != NULL){ 
		int fdOut = creat(cp->stdout_file, 0664); //rw r r permissions
		dup2( fdOut, STDOUT_FILENO);
		close(fdOut);
	}	

	//call execvp with command
	execvp(cp->path, cp->argv);
	//following executes only if there was an error and process was not terminated
	printf("Failed to execute command '%s'.\n", cp->path);
	exit(1);
}

//recursive piping 
void pipeCommands(Command** cp, int fdInput){
	//terminating condition : separator is not a pipe, or theres no commands following the current command
	if ((*cp)->separator != '|' || (*cp)->nextCmd == NULL){
		//duplicate file descriptor into stdin
		if (dup2(fdInput, STDIN_FILENO) == -1) {
			printf("Error duplicating file descriptor.\n");
			exit(1);
		}					
		executeCommand((*cp));
	} else {
		//create a pipe
		int fdPipe[2];
		if (pipe(fdPipe) == -1){
			printf("Error duplicating pipe.\n");
			exit(1);
		} else {
			//fork and let parent call pipeCommands with the next command recursively
			pid_t pid = fork();
			if (pid > 0){ //parent executes next command, closes write and fdInput
				close(fdPipe[1]);
				close(fdInput);
				
				cp = &(*cp)->nextCmd;
				
				pipeCommands(cp, fdPipe[0]);
				close(fdPipe[0]);
			} else if (pid == 0) { //child executes current command
				close(fdPipe[0]); //close read as its not used
				if (dup2(fdInput, STDIN_FILENO) == -1){
					printf("Error duplicating file descriptor.\n");
					exit(1);
				} //read from fdInput parameter
				if (dup2(fdPipe[1], STDOUT_FILENO) == -1){
					printf("Error duplicating file descriptor.\n");
					exit(1);
				}	//duplicate output from pipe
	
				executeCommand((*cp));
			} else {
				printf("Error forking pipe.\n");
				exit(1);
			}
		}
	}
}
