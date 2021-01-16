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
	raw.c_iflag &= ~(BRKINT); // Disable BREAK handling
	raw.c_iflag &= ~(ICRNL);  // Diable translation of carriage return to newline
	raw.c_iflag &= ~(INPCK);  // Disable input parity checking
	raw.c_iflag &= ~(ISTRIP); // Disable stripping off of 8th bit
	raw.c_iflag &= ~(IXON);   // Disable control keys to pause/resume transmission

	raw.c_oflag &= ~(OPOST);  // Diable implementation-defined output processing

	raw.c_lflag &= ~(ECHO);   // Don't echo keys to terminal
	raw.c_lflag &= ~(ICANON); // Disable canonical mode (input is available immediately)
	raw.c_lflag &= ~(IEXTEN); // Disable implementation-defined input processing
	raw.c_lflag &= ~(ISIG);   // Disiable signals

	raw.c_cflag |= (CS8);     // Set character size mask to 8 bits

	raw.c_cc[VMIN] = 0;       // Set minimum number of bytes before read() can return
	raw.c_cc[VTIME] = 1;      // Set read() timeout to 1/10th of a second
	
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

	while (1)
	{
		char c = '\0';
		read(STDIN_FILENO, &c, 1);

		if (iscntrl(c))
			printf("%d\r\n", c);
		else
			printf("%d ('%c')\r\n", c, c);

		if (c == 'q')
			break;
	}

	return 0;
}
