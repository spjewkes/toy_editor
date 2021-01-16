toy_editor: main.c
	gcc $< -o $@ -Wall -Wextra -pedantic -std=c99

clean:
	rm -f *~
	rm -f *.o
