// Author:      Oliver Graham
// Course:      CIS 344 / Winter 2022
// Assignment:  3 - smallsh
// Due Date:    2/7/2022
// Description: Implementation file. Presents a command line for a user. Contains some built-in commands,
//				and has the capability to fork off child processes to execute other commands.

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include "smallsh.h"


/* Global variables. I've preprended with g to denote their globality.
*  I spent days (and days) getting the majority of this working by passing pointers only.
*  But it turned out to be having memory issues, so I reverted back to using these global variables.
*/
static bool gForegroundOnly = false;
static int gExitStatus;					
static struct command* gCommand;
static struct sigaction gSignalActionSigInt = { {0} };
static struct sigaction gSignalActionSigStp = { {0} };
static pid_t gBackgroundProcessIds[1000];
static int gBackgroundIndex = 0;


/* Given the user input string, loops character by character, adding to the command struct */
void addStringToCommand(char* input, const char* pidAsString,
	int inputLength, int pidLength, int argumentIndex, int* inputStartIndex) {

	int argCharIndex = 0;

	while (*inputStartIndex < inputLength && input[*inputStartIndex] != ' ') {

		// expand dollar signs if applicable
		if (input[*inputStartIndex] == '$' && input[*inputStartIndex + 1] == '$') {

			// add pid into provided space in argument
			for (int i = 0; i < pidLength; i++) {
				gCommand->arguments[argumentIndex][argCharIndex] = pidAsString[i];
				argCharIndex++;
			}

			*inputStartIndex += 2;		// get past 2 dollar signs
		}
		else {
			// add character normally
			gCommand->arguments[argumentIndex][argCharIndex] = input[*inputStartIndex];
			argCharIndex++;
			(*inputStartIndex)++;
		}
	}

	// know where to start from last time; skip the space that was encountered
	(*inputStartIndex)++;
}

/* Allocates memory for arguments. Attempts to realloc only when necessary. */
void allocateMemoryToCommand(int argumentIndex, int newSize) {

	if (gCommand->arguments[argumentIndex] != NULL) {

		// compare length of previous command argument to new input
		int oldCommandLength = strlen(gCommand->arguments[argumentIndex]);

		// if the new input is smaller than the old, null the existing characters before reallocating
		if (newSize < oldCommandLength) {

			for (int i = 0; i < oldCommandLength; i++) {
				gCommand->arguments[argumentIndex][i] = NULL;
			}

		}
		// If new input is larger or smaller, realloc. Don't realloc if they're the same size.
		if (newSize != oldCommandLength) {
			gCommand->arguments[argumentIndex] = realloc(gCommand->arguments[argumentIndex], newSize + 1);
		}
	}
	else {
		gCommand->arguments[argumentIndex] = calloc(newSize + 1, sizeof(char));
	}
}

/* Changes to HOME directory or directory provided */
void builtInCd(char* directoryPath) {

	// go home if no argument provided
	if (directoryPath == NULL) {
		chdir(getenv("HOME"));		
	}
	// go to directory or print error message
	else if (chdir(directoryPath) == -1) {
		printf("Cannot find directory %s\n", directoryPath);
		fflush(stdout);
	}
}

/* Kill processes, free memory and exit shell */
int builtInExit(char* userInput, char* pidAsString) {

	for (int i = 0; i < gBackgroundIndex; i++) {
		kill(gBackgroundProcessIds[i], SIGTERM);
	}

	int j = 0;
	while (gCommand->arguments[j] != NULL) {
		free(gCommand->arguments[j]);
		gCommand->arguments[j] = NULL;
		j++;
	}
	
	free(gCommand);
	gCommand = NULL;

	free(userInput);
	userInput = NULL;

	free(pidAsString);
	pidAsString = NULL;

	exit(gExitStatus);
}

/* Loops through input string, determining the number of characters that will need to be allocated.
*  Considers what the length of the string would be after variable expansion.
*/
int determineMaxLength(char* input, int inputLength, int pidLength, int start) {

	int maxLength = 0;

	// delimit by space character
	while (start < inputLength && input[start] != ' ') {
	
		if (input[start] == '$' && input[start + 1] == '$') {
			maxLength += pidLength - 1;			// subtract one to account for subsequent increment
			start++;							// need extra skip for the second $
		}
		start++;
		maxLength++;
	}	

	return maxLength;		
}

/* Given valid user input, this function will parse it and populate a command struct */
void createCommand(char* input, const char* pidAsString) {

	// if is background, set flag in struct and nullify that character in input
	int inputLength = strlen(input);
	if (input[inputLength - 1] == '&') {

		// foreground-only mode can be set by the SIG_STP signal handlers
		if (!gForegroundOnly) {
			gCommand->isBackground = true;
		}

		// want to take out this character from the input regardless
		input[inputLength - 1] = NULL;
		inputLength--;
	}

	int inputIndex = 0;
	int pidLength = strlen(pidAsString);	
	int argumentIndex = 0;

	while (inputIndex < inputLength) {

		// get length of string argument - considers variable expansion
		int currentMaxLength = determineMaxLength(input, inputLength, pidLength, inputIndex);

		// allocate memory, pricesly-sized 
		allocateMemoryToCommand(argumentIndex, currentMaxLength);

		// advances inputIndex in function
		addStringToCommand(input, pidAsString, inputLength, pidLength, argumentIndex, &inputIndex);

		argumentIndex++;
	}

	// if there are too many arguments from last command, free and null
	int k = gCommand->numberOfArguments - 1;			
	while (k >= argumentIndex) {
		free(gCommand->arguments[k]);
		gCommand->arguments[k] = NULL;
		k--;
	}

	// save this information
	gCommand->numberOfArguments = argumentIndex;
}

/* Uses printf to display the desired prompt. */
void displayPrompt() {
	printf(": ");
	fflush(stdout);
}

/* Forked process handles redirection and installs the ignore signal for ^C before executing a command */
void executeChildProcess() {
	
	// will parse arguments and determine if redirection should occur. If so,
	// it will perform the necessary actions.
	handleRedirection();

	// if the command is a foreground command, ignore the sigint signal
	// here in this child process
	if (!gCommand->isBackground) {

		// install new signal handler
		gSignalActionSigInt.sa_handler = SIG_DFL;		
		sigaction(SIGINT, &gSignalActionSigInt, NULL);
	}

	// possibly necessary to append a null to the arguments
	char** argv = { gCommand->arguments, NULL };

	if (execvp(gCommand->arguments[0], argv)) {
		printf("%s: no such file or directory\n", gCommand->arguments[0]);
		fflush(stdout);
		gExitStatus = 1;		// set to 1 per the specifications
		exit(gExitStatus);		
	}
}

/* Waits for child process to complete or continues forward with execution */
void executeParentProcess(bool isBackground, pid_t childPid) {

	if (isBackground) {
		waitpid(childPid, &gExitStatus, WNOHANG);	  // no hang, keep going
		printf("background pid is %d\n", childPid);
		fflush(stdout);
	}
	else {
		waitpid(childPid, &gExitStatus, 0);			  // foreground; wait for child 
	}
}

/* Gets the pid from the current process and converts the digits to a string, which it returns. */
const char* getPidAsString() {

	pid_t pid = getpid();

	int theDigits = pid;
	int numberOfDigits = 1;
	
	while (theDigits != 0) {
		theDigits /= 10;		// chop off rightmost digit
		numberOfDigits++;				
	}

	// size the memory and convert the number to a string
	char* pidAsString = malloc(numberOfDigits + 1);
	snprintf(pidAsString, numberOfDigits, "%d", pid);

	// const, as a reminder not to modify this string. Maybe its pointless
	return (const) pidAsString;
}

/* Gets user input and sanitizes somewhat. Does not parse string into commands. */
void getUserInput(char* userInput) {

	fflush(stdin);	
	size_t bufferLength;
	int bytesRead = getline(&userInput, &bufferLength, stdin);
	
	// check if user hit enter only (newline only)
	if (strcmp(userInput, "\n") == 0) {
		*userInput = NULL;
	}		

	// check first character to see if line is a comment
	if (*userInput == '#') {
		*userInput = NULL;
	}		

	// remove newline character (length > 1 guaranteed at this point)
	userInput[bytesRead - 1] = NULL;	
}

/* Called by ^Z (SIGSTP). Toggles between foreground-only mode and allowing background commands.
*  Uses reentrant function to write output.
*/
void handleControlZ(int signo) {

	if (!gForegroundOnly) {
		char const message[] = "\nEntering foreground - only mode(& is now ignored)\n";
		write(STDOUT_FILENO, message, sizeof message - 1);
	}
	else {
		char const message[] = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, sizeof message - 1);
	}

	// toggle on/off
	gForegroundOnly = !gForegroundOnly;

	// TODO
	// the regular display prompt
	// seems to aid in output when running manually, adds extra : when running via script
	char const prompt[] = ": ";
	write(STDOUT_FILENO, prompt, sizeof prompt - 1);
}

/* Parses given arguments for redirection characters and file names.
*  Will open/close read and write files after determining redirection. 
*/
void handleRedirection() {
	
	char* inputFile = NULL;
	char* outputFile = NULL;
	
	// start at first argument
	int i = 1;
	while (gCommand->arguments[i] != NULL) {
		
		if (*gCommand->arguments[i] == '<') {

			// remove redirection character '<'
			free(gCommand->arguments[i]);
			gCommand->arguments[i++] = NULL;	

			// guard against attempts for multiple file redirects (only take first one)
			if (inputFile == NULL) {
				inputFile = malloc(strlen(gCommand->arguments[i]) + 1);
				strcpy(inputFile, gCommand->arguments[i]);
			}

			// null the argument, so it's not passed to the exec() function later on
			free(gCommand->arguments[i]);
			gCommand->arguments[i] = NULL;
		}
		else if (*gCommand->arguments[i] == '>') {

			// remove redirection character '>'
			free(gCommand->arguments[i]);
			gCommand->arguments[i++] = NULL;	

			// guard against attempts for multiple file redirects (only take first one)
			if (outputFile == NULL) {
				outputFile = malloc(strlen(gCommand->arguments[i]) + 1);
				strcpy(outputFile, gCommand->arguments[i]);
			}

			// null the argument, so it's not passed to the exec() function later on
			free(gCommand->arguments[i]);
			gCommand->arguments[i] = NULL;
		}
		i++;
	};
	
	// is checked later to determine if anything went wrong in the following operations
	int redirectResult = 0;

	// background commands (if < or > aren't specified) go to dev/null
	// otherwise, they are redirected to the input/output files are specified
	if (inputFile == NULL && gCommand->isBackground) {

		// attempt to open file as read-only
		int fileDescriptor = open("/dev/null", O_RDONLY);

		redirectResult = redirectFile(fileDescriptor, "/dev/null", "input to dev/null", 0);
	}
	else if (inputFile != NULL) {

		// attempt to open file as read-only
		int fileDescriptor = open(inputFile, O_RDONLY);

		// calls dup2() for redirection
		redirectResult = redirectFile(fileDescriptor, inputFile, "input", 0);
		free(inputFile);
		inputFile = NULL;
	}

	if (outputFile == NULL && gCommand->isBackground) {

		// attempt to open according to specifications, with permissions set to -rw-rw-rw-
		int fileDescriptor = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0666);

		// calls dup2() for redirection
		redirectResult = redirectFile(fileDescriptor, "/dev/null", "output to dev/null", 1);
	}
	else if (outputFile != NULL) {

		// attempt to open according to specifications, with permissions set to -rw-rw-rw-
		int fileDescriptor = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);

		// calls dup2() for redirection
		redirectResult = redirectFile(fileDescriptor, outputFile, "output", 1);
		free(outputFile);
		outputFile = NULL;
	}

	// if file couldn't read/write for any reason, exit forked process now
	if (redirectResult == -1) {
		exit(gExitStatus);
	}
}

/* Initializes sigaction struct for ^C (SIGINT) */
void initializeControlCHandler() {

	gSignalActionSigInt.sa_handler = SIG_IGN;			// want to ignore this signal
	sigfillset(&gSignalActionSigInt.sa_mask);			// block all catchable signals while running
	gSignalActionSigInt.sa_flags = 0;					// no flags
	sigaction(SIGINT, &gSignalActionSigInt, NULL);		// install the signal handler
}

/* Initializes sigaction struct for ^Z (SIGSTP) */
void initializeControlZHandler() {

	gSignalActionSigStp.sa_handler = handleControlZ;	// attach function
	sigfillset(&gSignalActionSigStp.sa_mask);
	gSignalActionSigStp.sa_flags = SA_RESTART;			// returning from a handler resumes the function when this flag is set	
	sigaction(SIGTSTP, &gSignalActionSigStp, NULL);
}

/* Forks and executes commands. Prints status of background processes. */
void otherCommands() {	

	pid_t childPid = fork();

	// save pid and increment index
	gBackgroundProcessIds[gBackgroundIndex++] = childPid;

	switch (childPid) {
		case -1:
			perror("error with fork()\n");
			exit(1);						// exit failed forked process
			break;
		case 0:
			executeChildProcess();
			break;
		default:
			executeParentProcess(gCommand->isBackground, childPid);
	}

	// wait for the child to finish but don't hang
	while ((childPid = waitpid(-1, &gExitStatus, WNOHANG)) > 0) {
		printf("background pid %d is done: ", childPid);
		fflush(stdout);
		printStatus();
	}

	// returns to main()
}

/* Prints the exit status; Uses macros to determine which message to print. */
void printStatus() {

	// print terminating signal of the last foreground process ran by the shell
	if (WIFSIGNALED(gExitStatus)) {
		printf("terminated by signal %i\n", WTERMSIG(gExitStatus));
		fflush(stdout);
	}

	// print exit status 
	if (WIFEXITED(gExitStatus)) {
		printf("exit value %i\n", WEXITSTATUS(gExitStatus));
		fflush(stdout);
	}
}

/* Redirects opened file using dup2().
*  fileType would be either input/output or to the dev/null.
*  redirectionLocation signifies stdin or stdout.
*/
int redirectFile(int fileDescriptor, char* fileName, char* fileType, int redirectionLocation) {

	if (fileDescriptor == -1) {
		printf("Cannot open %s for %s\n", fileName, fileType);
		fflush(stdout);
		gExitStatus = 1;
		return fileDescriptor;
	}

	// redirect stdin to source file
	int redirectResult = dup2(fileDescriptor, redirectionLocation);	 // stdin == 0; stdout == 1		
	if (redirectResult == -1 && fileDescriptor != -1) {				 // only print if dup2() produced the error
		printf("Error with dup2()\n");
		fflush(stdout);
		gExitStatus = 1;
		return redirectResult;
	}

	// schedule its close
	return fcntl(fileDescriptor, F_SETFD, FD_CLOEXEC);
}

/* Initializes structs and other variables and runs continous loop to obtain user input and execute commands.
*  Exits only when user types appropriate command.
*/
int main(void) {	

	// 2048 per the specifications
	char* userInput = malloc(2048);
	const char* pidAsString = getPidAsString();

	// get the structs going
	initializeControlCHandler();
	initializeControlZHandler();
	gCommand = malloc(sizeof(struct command));

	while (true) {
		
		*userInput = NULL;
		do {			
			displayPrompt();
			getUserInput(userInput);
		} while (*userInput == NULL);
		

		// disregard possible extraneous arguments after "exit"
		if (strncmp(userInput, "exit", 4) == 0) {
			return builtInExit(userInput, pidAsString);
		}

		// make sure this resets to false
		gCommand->isBackground = false;

		// stores command and arguments; expands $$ if necessary
		createCommand(userInput, pidAsString);
		
		// index 0 will always contain the command
		if (strcmp(gCommand->arguments[0], "cd") == 0) {
			
			// index 1 will contain the user's desired directory to change to
			builtInCd(gCommand->arguments[1]);
		}
		else if (strcmp(gCommand->arguments[0], "status") == 0) {
			printStatus();
		}
		else {
			otherCommands();
		}				
	}
}