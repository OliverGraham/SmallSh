// Author:      Oliver Graham
// Course:      CIS 344 / Winter 2022
// Assignment:  3 - smallsh
// Due Date:    2/7/2022
// Description: Header file for smallsh.c


// The goal was to create a single command that could house all the information,
// and that the space allocated here would be reused.
static struct command {
	char* arguments[512];
	bool isBackground;		 // will default to false; will be changed as needed
	int numberOfArguments;
};

/* Given the user input string, loops character by character, adding to the command struct */
void addStringToCommand(char* input, const char* pidAsString,
	int inputLength, int pidLength, int argumentIndex, int* inputStartIndex);

/* Allocates memory for arguments. Attempts to realloc only when necessary. */
void allocateMemoryToCommand(int argumentIndex, int newSize);

/* Changes to HOME directory or directory provided */
void builtInCd(char* directoryPath);

/* Kill processes and exit shell */
int builtInExit(char* userInput, char* pidAsString);

/* Loops through input string, determining the number of characters that will need to be allocated.
*  Considers what the length of the string would be after variable expansion.
*/
int determineMaxLength(char* input, int inputLength, int pidLength, int start);

/* Given valid user input, this function will parse it and populate a command struct */
void createCommand(char* input, const char* pidAsString);

/* Uses printf to display the desired prompt. */
void displayPrompt();

/* Forked process handles redirection and installs the ignore signal for ^C before executing a command */
void executeChildProcess();

/* Waits for child process to complete or continues forward with execution */
void executeParentProcess(bool isBackground, pid_t childPid);

/* Replaces any instance of two dollar signs in the input with the process id  */
//void expandDollarSign(struct command* currentCommand, const int argumentIndex, const char* token, const char* pidAsString);
void expandDollarSign(const int argumentIndex, const char* token, const char* pidAsString);

/* Gets the pid from the current process and converts the digits to a string, which it returns. */
const char* getPidAsString();

/* Gets user input and sanitizes somewhat. Does not parse string into commands. */
void getUserInput();

/* Called by ^Z (SIGSTP). Toggles between foreground-only mode and allowing background commands.
*  Uses reentrant function to write output.
*/
void handleControlZ(int signo);

/* Parses given arguments for redirection characters and file names.
*  Will open/close read and write files after determining redirection.
*/
void handleRedirection();

/* Initializes sigaction struct for ^C (SIGINT) */
void initializeControlCHandler();

/* Initializes sigaction struct for ^Z (SIGSTP) */
void initializeControlZHandler();

/* Forks and executes commands. Prints status of background processes. */
void otherCommands();

/* Prints the exit status with a corresponding message. */
void printStatus();

/* Redirects opened file using dup2().
*  fileType would be either input/output or to the dev/null.
*  redirectionLocation signifies stdin or stdout.
*/
int redirectFile(int fileDescriptor, char* fileName, char* fileType, int redirectionLocation);