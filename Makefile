build:
	@gcc fuzzy.c term_escapes.c term_mode.c main.c -o fuzzy -Wall -Wextra

leak_check:
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ./fuzzy
