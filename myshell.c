/****************************************************************
 * Name        : Alex Wolski                                    *
 * Class       : CSC 415                                        *
 * Date        : 10/24/18                                       *
 * Description : Writting a simple bash shell program           *
 *               that will execute simple commands. The main    *
 *               goal of the assignment is working with         *
 *               fork, pipes and exec system calls.             *
 ****************************************************************/

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
//Used for getting the terminal dimensions
#include <sys/ioctl.h>
#include <fcntl.h>
//Needed to use the waitpid function
#include <sys/types.h>
#include <sys/wait.h>
//Needed for getting the home directory
#include <pwd.h>
//Needed for changing the shell to 'raw' mode
#include <termios.h>

//Maximum size of the input the user can enter
#define BUFFERSIZE 256
//The string and size of the string used to prompt the user
#define PROMPT "myShell "
#define PROMPTSIZE sizeof(PROMPT)
//Colors for the prompt
#define GREEN "\033[32m"
#define BLUE "\033[34m"
#define RESETCOLOR "\x1b[0m"
//Simple keywords for booleans
#define true 1
#define false 0



//Contain all of the commands and thier information from an input
struct command
{
  //A 2D array to store all of the commands entered
  char*** commandTable;
  //The file the first command should read from
  char* inputFile;
  //The file the last command should write to
  char* outputFile;
  //Boolean to determine if the last command should append (1) to or truncate (0) the output file
  int append;
  //Boolean to determine if all of the commands should run in the background (1) or not (0)
  int background;
};

//Containts the history of commands run and the current history being viewed
struct history
{
  char** history;
  int totalItems;
};

//Functions declerations
struct termios* rawShell();
void resetShell(struct termios* oldSettings);
int getInput(char* prompt, char* input, int maxSize, struct history* commandHistory);
int parseCommands(char* string, struct command* allCommands, int commandSize);
void execute(struct command* allCommands, int numCommands);

int main(int argc, char** argv)
{
  //Store what the user inputs
  char* input = malloc(BUFFERSIZE);
  //Create a structure to hold all of the commands.
  //Each command in the command table could use as much memory as the input.
  //So allocate the command table with enough memory to hold the maximum number of commands with all with the same length as  input
  struct command allCommands = { malloc(BUFFERSIZE*BUFFERSIZE/2) };
  //Change the shell to raw mode and save the previous settings
  struct termios* oldSettings = rawShell();
  //Stores all of the previous commands run
  struct history commandHistory = { malloc(1000000), 0 };
  //Used to hold the number of commands in the struct
  int numCommands;
  
  while (1)
  {
    //Reset the variables in the struct. Use memset to set all of the elements of the 2D array to 0
    memset(allCommands.commandTable, 0, BUFFERSIZE*BUFFERSIZE/2);
    allCommands.inputFile = NULL;
    allCommands.outputFile = NULL;
    allCommands.append = false;
    allCommands.background = false;
    
    //Reset the input string
    memset(input, 0, BUFFERSIZE);
    
    //Get an input from the user. If the user wants to exit the program, then exit.
    if(!getInput(PROMPT, input, BUFFERSIZE, &commandHistory))
    {
      //Change the settings of the terminal back to what they were
      resetShell(oldSettings);
      return 0;
    }
    
    //Parse the input into the struct and store the number of commands parsed
    //The worst case is that the entire input is in one command, so allocate each command the same amount of memory as the input
    numCommands = parseCommands(input, &allCommands, BUFFERSIZE);

    //Execute the commands if there are any
    if(numCommands > 0)
      execute(&allCommands, numCommands);
  }

  //Change the settings of the terminal back to what they were
  resetShell(oldSettings);
  return 0;
}

//Change the shell to raw mode so that inputs get sent to the shell and not output to the screen
struct termios* rawShell()
{ 
  //Variables that hold the termios structs. Use the size of an unintialized termios struct to allocate memory to oldSettings
  struct termios newSettings;
  struct termios* oldSettings = malloc(sizeof(newSettings));
  tcgetattr(STDIN_FILENO, oldSettings);
  memcpy(&newSettings, oldSettings, sizeof(newSettings));
  //Add the options to make the terminal raw
  newSettings.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ECHOPRT | ECHOKE | ICRNL);
  //Apply the settings
  tcsetattr(STDIN_FILENO, TCSANOW, &newSettings);

  return oldSettings;
}

//Reset the previous state of the terminal
void resetShell(struct termios* oldSettings)
{
  tcsetattr(STDIN_FILENO, TCSANOW, oldSettings);
}

//Return the current directory. Helper function for getInput
char* getCurrDirectory()
{
  //Full working directory
  char* fullDirectory = malloc(4096);
  getcwd(fullDirectory, 4096);

  //Get the length of the working directory
  int fullLength = 0;

  while(fullDirectory[fullLength] != '\0')
    fullLength++;
  
  //Home directory
  char* homeDirectory = getenv("HOME");
  //Get the length of the home directory
  int homeLength = sizeof(homeDirectory);
  
  //If the home directory is a substring of the current directory, simplify the current directory
  if(!strncmp(fullDirectory, homeDirectory, homeLength) && fullLength >= homeLength)
  {
    //Working directory excluding the home direcory. It is the length of the full directory - home directory + ~
    char* simplifiedDirectory = malloc(fullLength - homeLength + 10);

    //Copy the second half of the full directory not including the home directory
    strncpy(simplifiedDirectory, fullDirectory + homeLength + 1, fullLength - homeLength);
    //Replace the first element with '~' to represent the home directory
    simplifiedDirectory[0] = '~';
    
    return simplifiedDirectory;
  }
  
  //If the home directory is not a substring, return the whole directory
  return fullDirectory;
}

void addStringArray(char** array, int targetIndex, char* item)
{
  int i = 0;
  
  //Keep iterating until i is at the end of the aray
  while(array[i] != NULL)
    i++;

  //Loop back trough the array and move each elemetn up one index
  for(; i > targetIndex; i--)
    array[i] = array[i-1];

  array[targetIndex] = item;
}

//Take an offset from the last column and move the cursor there
void moveCursor(struct winsize* window, int* newOffset, int* totalLength)
{
  //Calclate the position of the cursor given its offset, the length of the command, and the dimensiosn of the terminal
  int newX = *newOffset % window->ws_col;
  int newY = window->ws_row - ((*totalLength / window->ws_col) - (*newOffset / window->ws_col));
  
  printf("\033[%d;%dH", newY, newX + 1);
}

//Move the cursor left on the screen
void left(struct winsize* window, int* cursorOffset, int* totalLength)
{
  (*cursorOffset)--;
  moveCursor(window, cursorOffset, totalLength);
}

//Move the cursor right on the screen
void right(struct winsize* window, int* cursorOffset, int* totalLength)
{
  (*cursorOffset)++;
  moveCursor(window, cursorOffset, totalLength);
}

//Not implemented yet
void backspace(int n, struct winsize* window, int* cursorOffset, int* totalLength)
{
}

//Not implemented yet
void delete(int n, struct winsize* window, int* cursorOffset, int* totalLength)
{
}

//Writes a string to the terminal and overwrites the existing text
void overWrite (char* string, int overWriteSize)
{
  //Clear the buffer
  fflush(stdout);
  
  int i = 0;

  //Print the string
  while(string[i] != '\0')
  {
    printf("%c", string[i]);
    i++;
  }

  //If the string doesn't clear the terminal, print spaces
  while(i < overWriteSize)
  {
    printf(" ");
    i++;
  }
}

//Add a command stored in a string the history data structure
void addHistory(struct history* commandHistory, char* command, int maxSize)
{
  commandHistory->history[commandHistory->totalItems] = malloc(maxSize);
  strcpy(commandHistory->history[commandHistory->totalItems], command);
  (commandHistory->totalItems)++;
}

//Prompt the user for an input and store it in the given buffer. If the return value is 0, the user wants to exit
int getInput(char* prompt, char* input, int maxSize, struct history* commandHistory)
{
  //Character that stores each key input
  char keyPressed;
  //Iterate through the array
  int i = 0;
  //Iterate through history array
  int historyIndex = commandHistory->totalItems;

  //Store the cursor's offset from the beginning of the prompt
  int cursorOffset = 0;
  int cursorMin = 0;
  int cursorMax = 0;
  //Store the dimensions of the terminal window
  struct winsize window;

  //Print the full prompt
  char* fullPrompt = malloc(5000);
  sprintf(fullPrompt, "%s%s%s%s%s >> ", GREEN, PROMPT, BLUE, getCurrDirectory(), RESETCOLOR);
  printf("%s", fullPrompt);

  //Iterate through the array to determine how long it is
  while(fullPrompt[cursorMin] != '\0')
    cursorMin++;

  //Account for the color characters in the string
  cursorMin -= 14;
  
  cursorMax = cursorMin;
  cursorOffset = cursorMin;
  
  //Keep reading inputs from the user while it is not a new line and add the character to the input array
  while((keyPressed = getchar()) != 10)
  {
    //Update the window size
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &window);
    
    //If the user enters "CTR+D", return false
    if(keyPressed == 4)
    {
      printf("\n");
      return false;
    }
    //If the user enters a control character
    else if(keyPressed == 27)
    {
      //Ignore the second input
      getchar();
      //Get the third one
      keyPressed = getchar();

      //If the user presses up, dispay the previous command
      if(keyPressed == 65)
      {
	if(historyIndex > 0)
	{
	  historyIndex--;
	  
	  cursorOffset = cursorMin;
	  moveCursor(&window, &cursorOffset, &cursorMax);
	  
	  char* currentHistory = commandHistory->history[historyIndex];
	  overWrite(currentHistory, cursorMax - cursorMin);

	  i = 0;
	  
	  while(currentHistory[i] != '\0')
	    i++;

	  cursorMax = cursorMin + i;
	  cursorOffset = cursorMax;
	  moveCursor(&window, &cursorOffset, &cursorMax);

	  strcpy(input, currentHistory);
	}
      }
      //If the user presses down, display the next command
      else if(keyPressed == 66)
      {
  	if(historyIndex < commandHistory->totalItems - 1)
	{
	  historyIndex++;
	  
	  cursorOffset = cursorMin;
	  moveCursor(&window, &cursorOffset, &cursorMax);
	  
	  char* currentHistory = commandHistory->history[historyIndex];
	  overWrite(currentHistory, cursorMax - cursorMin);

	  i = 0;
	  
	  while(currentHistory[i] != '\0')
	    i++;

	  cursorMax = cursorMin + i;
	  cursorOffset = cursorMax;
	  moveCursor(&window, &cursorOffset, &cursorMax);

	  strcpy(input, currentHistory);
	}
	else
	{
	  cursorOffset = cursorMin;
	  moveCursor(&window, &cursorOffset, &cursorMax);
	  overWrite("", cursorMax - cursorMin);
	  moveCursor(&window, &cursorOffset, &cursorMax);
	}
      }
      //If the user presses right, move the cursor right
      else if(keyPressed == 67)
      {
	if(cursorOffset < cursorMax)
	{
	  i++;
	  right(&window, &cursorOffset, &cursorMax);
	}
      }
      //If the user presses left, move the cursor left
      else
      {
	if(cursorOffset > cursorMin)
	{
	  i--;
	  left(&window, &cursorOffset, &cursorMax);
	}
      }
    }
    //If the user hits backspace, do nothing
    else if(keyPressed == 127)
    {
    }
    //If the key pressed is a valid character, append it to the input string
    else if(keyPressed > 31)
    {
      //Add the character to the first open index
      input[i] = keyPressed;
      //Print the character just typed
      printf("%c", keyPressed);
      
      i++;
      cursorMax++;
      cursorOffset++;
    }
  }
  
  printf("\n");
  
  //If the user enters "exit", return false
  if(!strcmp(input, "exit"))
    return false;

  //After the command is processes, add it to the history
  addHistory(commandHistory, input, maxSize);
  
  return true;
}

//Take a string of commands and arguments and parse it into a command struct
int parseCommands(char* string, struct command* allCommands, int commandSize)
{
    //Integers to iterate through the 2D array
    int command = 0;
    int argument = 1;
    //Delimiters to split the input string
    const char* delims = " ";
    //Store the current token being processed
    char* token;
    
    //Create the first row and set its first index to the first token.
    allCommands->commandTable[0] = malloc(commandSize);
    allCommands->commandTable[0][0] = strtok(string, delims);
    
    //While there are tokens left, keep looping and populate the array with the tokens
    while((token = strtok(NULL, delims)) != NULL)
    {
      //If the current token is a new pipe symbol, create a new array and start counting from the beginning of that array
      if(!strcmp(token, "|"))
      {
	command++;
	argument = 1;
	allCommands->commandTable[command] = malloc(commandSize);
	allCommands->commandTable[command][0] = strtok(NULL, delims);
      }
      //If an input file is specified, store it in the struct
      else if(!strcmp(token, "<"))
	allCommands->inputFile = strtok(NULL, delims);
      //If an output file is specified, store it in the struct
      else if(!strcmp(token, ">"))
	allCommands->outputFile = strtok(NULL, delims);
      //If an output file is specified in append mode, store it in the struct and toggle the append boolean
      else if(!strcmp(token, ">>"))
      {
	allCommands->outputFile = strtok(NULL, delims);
	allCommands->append = true;
      }
      //Otherwise, add the token to the array
      else
      {
	//If the token starts with a quote, keep adding tokens to it until the end quote is found
        if(token[0] == '\"' || token[0] == '\'')
	{
	  char quote = token[0];
	  
	  //Move each char back an element and overwrite the beginning quote
	  for(int i = 0; token[i] != '\0'; i++)
	    token[i] = token[i+1];

	  //Keep adding the next token until the end quote is found
	  //If the token starts with a double quote, search for another double quote
	  if(quote == '\"')
	    while(token[strlen(token)-1] != '\"')
	      sprintf(token, "%s %s", token, strtok(NULL, delims));
	  //If the token starts with a single quote, search for another single quote
	  else
	    while(token[strlen(token)-1] != '\'')
	      sprintf(token, "%s %s", token, strtok(NULL, delims));

	  //Remove the last quote
	  token[strlen(token)-1] = '\0';
	}
	
        allCommands->commandTable[command][argument] = token;
	argument++;
      }
    }

    if(allCommands->commandTable[0][0] == NULL)
      return 0;
    
    //If the last argument is '&', toggle the background boolean in the struct
    if(argument > 0 && !strcmp(allCommands->commandTable[command][argument-1], "&"))
    {
      allCommands->background = true;
      //Remove '&' from the command
      allCommands->commandTable[command][argument-1] = 0;
    }

    //If the last command is ls or grep, add the option to enable color for commands
    char* firstCommand = allCommands->commandTable[command][0];
    
    if(!strcmp(firstCommand, "ls") || !strcmp(firstCommand, "grep"))
    {
      //Set the second element to the color option
      addStringArray(allCommands->commandTable[command], 1, "--color=auto");
    }
    
    //Return the number of commands in the array
    return command + 1;
}

/*Built-In shell commands*/

//Print the directory the shell is currently in
void printWorkingDirectory()
{
  //Create a string to store the file path. The maximum filepath length is 4096 byte.
  char* input = malloc(4096);
  getcwd(input, 4096);
  printf("%s\n", input);
}

//Change the directory the shell is currently in
void changeDirectory(char* destination)
{
  chdir(destination);
}

void redirectFD(int newDest, int oldDest)
{
  dup2(newDest, oldDest);
  close(newDest);
}

void createPipe(int* readEnd, int* writeEnd)
{
  //Create a new pipe
  int newPipe[2];
  pipe(newPipe);

  //Store the read and write ends of the pipe
  *readEnd = newPipe[0];
  *writeEnd = newPipe[1];
}

//Execute the command. General algorithm came from:
//https://www.cs.purdue.edu/homes/grr/SystemsProgrammingBook/Book/Chapter5-WritingYourOwnShell.pdf
void execute(struct command* allCommands, int numCommands)
{
  //Store the defualt input and outfile locations
  int defaultIn = dup(STDIN_FILENO);
  int defaultOut = dup(STDOUT_FILENO);
  //Store the input and output locations for each process
  int input;
  int output;
  //Hold the process id of the child process
  int childProcess;

  //If there is no input file stored in the struct, use the default input
  if(allCommands->inputFile != NULL)
    input = open(allCommands->inputFile, O_RDONLY);
  //Otherwise, store its file id in the 'input' input
  else
    input = dup(defaultIn);

  //Loop through all of the commands in the struct
  for(int i = 0; i < numCommands; i++)
  {
    //If this is the first process, direct the input to the location determined above
    //If not, direct the input to the location inherited from the previous iteration
    redirectFD(input, STDIN_FILENO);

    //If the current command isn't the last, create a pipe. This process will write to the pipe and the child will inheir 'input'
    if(i != numCommands - 1)
      createPipe(&input, &output);
    //If the current command is the last command, determine where the output should go
    else
    {
      //If there is no output file stored in the strut, set the output to the default output location
      if(allCommands->outputFile == NULL)
	output = dup(defaultOut);
      //If there is an output file specified and the append boolean is true, open the file with the append flag
      else if(allCommands->append)
	output = open(allCommands->outputFile, O_CREAT | O_WRONLY | O_APPEND, 0666);
      //If there is an output file specifid and the append boolean is flase, open the file with the truncate flag
      else
	output = open(allCommands->outputFile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    }

    //After the output location is determined (default location, a file, or a pipe), redirect the program's output to that location
    redirectFD(output, STDOUT_FILENO);
    
    //If the current command is 'pwd', run the built in pwd function
    if(!strcmp(allCommands->commandTable[i][0], "pwd"))
      printWorkingDirectory();
    //If the current command is 'cd', run the built in cd function
    else if(!strcmp(allCommands->commandTable[i][0], "cd"))
      changeDirectory(allCommands->commandTable[i][1]);
    //If the command is not built into the shell, create a child process, find the program, and execute it
    else
    {
      //Create a child process
      childProcess = fork();
      
      //If the current process is the child, execute the command
      if(!childProcess)
      {
        execvp(allCommands->commandTable[i][0], allCommands->commandTable[i]);
        perror("");
        exit(1);
      }
    }
  }

  //After all of the commands are run, reset the defaut input and output files
  redirectFD(defaultIn, STDIN_FILENO);
  redirectFD(defaultOut, STDOUT_FILENO);

  //If the background boolean is not set in the struct, wait for the last process to finish before returning
  if(!allCommands->background)
    waitpid(childProcess, NULL, 0);
}
