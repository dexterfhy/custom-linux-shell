#ifndef COMMAND_H
#define COMMAND_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>

#define MAX_NUMBER_ARGUMENTS 100*1000

typedef struct CommandStructure {
    char* path;			// the path of the executable for command
    char separator;     // the command separator that follows the command. It should be 
                        // one of the following
                        //  "|"   - pipe  to the next command
                        //  "&"   - shell does not wait for this command
                        //  ";"   - shell wait for this command
						// the last command will automatically be given a sequential ';' separator
	int argc;			// the number of arguments
    char **argv;        // an array of tokens that forms a command
    char *stdin_file;   // if not NULL, points to the file name for stdin redirection                        
    char *stdout_file;  // if not NULL, points to the file name for stdout redirection 
	struct CommandStructure* nextCmd;   // type name for the command structure
} Command;

//returns number of commands, or -1 if error
int separateCommands(char* tokens[], Command* first);

//returns 1 if a token is one of three separators, 0 otherwise
int isSeparator(char token);

//sets stdin_file and stdout_file to relevant streams based on redirection symbols found
void searchRedirection(char *token[], Command *cp, int first, int last); 

//dynamically allocates array of char pointers to command's argv char** variable
void buildCommandArgumentArray(char *token[], Command *cp, int first, int last); 

//sets all values in a CommandStructure to default values
void initializeCommand(Command* cp);

//execute the command argument
void executeCommand(Command* cp);

//recursive pipe function used to execute a series of commands connected by pipe
void pipeCommands(Command** cp, int fdInput);
#endif
