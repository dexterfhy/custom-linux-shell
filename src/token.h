#ifndef TOKEN_H
#define TOKEN_H

#include <string.h>

#define MAX_NUM_TOKENS 100*1000 //100 commands, 1000 arguments each

//used to convert a string into a token array
int tokenise(char* inputLine, char* token[]);

#endif
