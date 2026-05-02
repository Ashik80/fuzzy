#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include "fuzzy.h"

typedef struct {
    char *text;
    int score;
} MatchedItem;

typedef struct {
    MatchedItem **items;
    size_t count;
    size_t size;
} MatchedItemList;

MatchedItemList list;
MatchedItemList matched_list;
struct termios orig_termios;
int tty_fd;
FILE *tty;

MatchedItem * add_score_to_item(char *text, int score) {
    MatchedItem *item = malloc(sizeof(MatchedItem));
    item->text = text;
    item->score = score;
    return item;
}

void init_matched_item_list(MatchedItemList *list) {
    list->size = 5;
    list->count = 0;
    list->items = malloc(list->size * sizeof(MatchedItem *));
}

void add_matched_item_to_list(MatchedItemList *list, MatchedItem *item) {
    if (list->count >= list->size) {
        list->size *= 2;
        list->items = realloc(list->items, list->size * sizeof(MatchedItem *));
    }
    list->items[list->count] = item;
    list->count++;
}

void print_matched_list_items(MatchedItemList *list, const int selected, const int rows, const size_t offset) {
    for (size_t i = offset; i < list->count && i < offset + rows; i++) {
        MatchedItem *item = list->items[i];
        if (i == selected) {
            fprintf(tty, "> %s: %d\n", item->text, item->score);
        } else {
            fprintf(tty, "  %s: %d\n", item->text, item->score);
        }
    }
}

int compare_match(const void *a, const void *b) {
    return (*(MatchedItem **)b)->score - (*(MatchedItem **)a)->score;
}

MatchedItemList sort_matched_item_list(MatchedItemList *list, const char *query) {
    MatchedItemList temp;
    init_matched_item_list(&temp);
    for (size_t i = 0; i < list->count; i++) {
        MatchedItem *item = list->items[i];
        item->score = fuzzy_score(item->text, query);
        if (item->score != NO_MATCH) {
            add_matched_item_to_list(&temp, item);
        }
    }
    qsort(temp.items, temp.count, sizeof(MatchedItem *), compare_match);
    return temp;
}

void free_matched_item_list(MatchedItemList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]->text);
        free(list->items[i]);
    }
    free(list->items);
}

char * copy_string(const char *src) {
    size_t len = strlen(src) + 1;
    char *mem = malloc(len);
    if (!mem) return NULL;
    memcpy(mem, src, len);
    return mem;
}

void read_from_input_or_pipe(MatchedItemList *list) {
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), stdin)) {
        if (buffer[strlen(buffer) - 1] == '\n') {
            buffer[strlen(buffer) - 1] = '\0';
        }
        MatchedItem *item = add_score_to_item(copy_string(buffer), 0);
        add_matched_item_to_list(list, item);
    }
}

void read_from_directory(MatchedItemList *list, char *base_path) {
    DIR *dir = opendir(base_path);
    if (dir == NULL) {
        printf("Failed to open directory: %s\n", base_path);
        exit(1);
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char *path = malloc(strlen(base_path) + strlen(entry->d_name) + 2);
        sprintf(path, "%s/%s", base_path, entry->d_name);
        if (entry->d_type == DT_DIR) {
            read_from_directory(list, path);
            free(path);
        } else {
            MatchedItem *item = add_score_to_item(path, 0);
            add_matched_item_to_list(list, item);
        }
    }
    closedir(dir);
}

void enable_raw_mode() {
    tcgetattr(tty_fd, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(tty_fd, TCSAFLUSH, &raw);
}

void disable_raw_mode() {
    tcsetattr(tty_fd, TCSAFLUSH, &orig_termios);
}

void enter_alternate_buffer() {
    fprintf(tty, "\033[?1049h");
}

void exit_alternate_buffer() {
    fprintf(tty, "\033[?1049l");
}

void hide_cursor() {
    fprintf(tty, "\033[?25l");
}

void show_cursor() {
    fprintf(tty, "\033[?25h");
}

void clear_screen() {
    fprintf(tty, "\033[H\033[J");
}

void restore_terminal(int sig) {
    free_matched_item_list(&list);
    free(matched_list.items); // only free the pointer, not items - owned by list
    disable_raw_mode();
    exit_alternate_buffer();
    show_cursor();
    fflush(tty);
    fclose(tty);
    exit(0);
}

int main() {
    signal(SIGINT, restore_terminal);

    tty = fopen("/dev/tty", "r+");
    if (!tty) {
        printf("Failed to open /dev/tty\n");
        exit(1);
    }
    tty_fd = fileno(tty);

    init_matched_item_list(&list);

    if (isatty(STDIN_FILENO)) {
        read_from_directory(&list, ".");
    } else {
        read_from_input_or_pipe(&list);
    }

    struct winsize w;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &w);
    int rows = w.ws_row - 2;
    char query[256] = {0};
    size_t len = 0;
    char c;
    size_t selected = 0;
    size_t offset = 0;

    enter_alternate_buffer();
    enable_raw_mode();
    hide_cursor();

    while (1) {
        clear_screen();
        fprintf(tty, "Query: %s\n", query);
        free(matched_list.items);
        matched_list = sort_matched_item_list(&list, query);
        print_matched_list_items(&matched_list, selected, rows, offset);
        fflush(tty);

        int read_result = read(tty_fd, &c, 1);
        if (read_result == -1) continue;
        if (read_result == 0) break;

        if (c == '\n') {
            if (matched_list.count <= 0) continue;
            disable_raw_mode();
            exit_alternate_buffer();
            show_cursor();
            fflush(tty);
            fclose(tty);
            printf("%s\n", matched_list.items[selected]->text);
            free_matched_item_list(&list);
            free(matched_list.items);
            exit(0);
        }
        if (c == '\033') {
            char seq[2];
            read(tty_fd, &seq[0], 1);
            read(tty_fd, &seq[1], 1);
            if (seq[0] == '[') {
                if (seq[1] == 'A') {
                    if (selected > 0) selected--;
                    if (selected < offset) offset--;
                }
                if (seq[1] == 'B') {
                    if (matched_list.count > 0 && selected < matched_list.count - 1) selected++;
                    if (selected >= offset + rows) offset++;
                }
            }
            continue;
        }
        if (c == 127) {
            if (len > 0) {
                len--;
                query[len] = '\0';
                selected = 0;
                offset = 0;
            }
        } else {
            if (len < sizeof(query) - 1) {
                query[len] = c;
                len++;
                query[len] = '\0';
                selected = 0;
                offset = 0;
            }
        }
    }

    free_matched_item_list(&list);
    free(matched_list.items);
    disable_raw_mode();
    exit_alternate_buffer();
    show_cursor();
    fclose(tty);

    return 0;
}
