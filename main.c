#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

/**
 * Defines
 */

/**
 * Data
 */

// Store original terminal settings
struct termios orig_termios;

// Print error and exit
void die(const char *s)
{
	perror(s);
	exit(1);
}

/**
 * Terminal
 */

// Disable terminal raw mode
void disableRawMode()
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		die("Failed to restore original terminal settings");
}

// Enable terminal raw mode
void enableRawMode()
{
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
		die("Failed to fetch original terminal settings");
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
		die("Failed to set terminal raw mode");
}

/**
 * Init
 */

// Main entry point
int main()
{
	enableRawMode();

	while (1)
	{
		char c = '\0';
		if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
			die("Failed to read byte");

		if (iscntrl(c))
			printf("%d\r\n", c);
		else
			printf("%d ('%c')\r\n", c, c);

		if (c == 'q')
			break;
	}

	return 0;
}
