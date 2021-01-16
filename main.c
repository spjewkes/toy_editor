#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

// Store original terminal settings
struct termios orig_termios;

// Disable terminal raw mode
void disableRawMode()
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		perror("Failed to restore original terminal settings");

	// Don't return a value from this function
}

// Enable terminal raw mode
int enableRawMode()
{
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
		return -1;
	atexit(disableRawMode);

	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO);   // Don't echo keys to terminal
	raw.c_lflag &= ~(ICANON); // Disable canonical mode (input is available immediately)
	
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		return -1;

	return 0;
}

// Main entry point
int main()
{
	if (enableRawMode() == -1)
	{
		perror("Error trying to enable terminal raw mode");
		return errno;
	}

	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q')
	{
		if (iscntrl(c))
			printf("%d\n", c);
		else
			printf("%d ('%c')\n", c, c);
	}
	
	return 0;
}
