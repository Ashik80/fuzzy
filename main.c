#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include "fuzzy.h"
#include "term_escapes.h"
#include "term_mode.h"

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
int tty_fd;
FILE *tty;

MatchedItem * add_score_to_item(char *text, int score) {
    MatchedItem *item = malloc(sizeof(MatchedItem));
    if (!item) {
        printf("Failed to allocate memory for matched item\n");
        exit(1);
    }
    item->text = text;
    item->score = score;
    return item;
}

void init_matched_item_list(MatchedItemList *list) {
    list->size = 5;
    list->count = 0;
    MatchedItem **mem = malloc(list->size * sizeof(MatchedItem *));
    if (!mem) {
        printf("Failed to allocate memory for matched item list\n");
        exit(1);
    };
    list->items = mem;
}

void add_matched_item_to_list(MatchedItemList *list, MatchedItem *item) {
    if (list->count >= list->size) {
        list->size *= 2;
        MatchedItem **mem = realloc(list->items, list->size * sizeof(MatchedItem *));
        if (!mem) {
            printf("Failed to allocate memory for matched item list\n");
            exit(1);
        };
        list->items = mem;
    }
    list->items[list->count] = item;
    list->count++;
}

void print_matched_list_items(MatchedItemList *list, const size_t selected, const int rows, const int cols, const size_t offset) {
    for (size_t i = offset; i < list->count && i < offset + rows; i++) {
        MatchedItem *item = list->items[i];
        char *text = item->text;
        size_t text_len = strlen(text);
        int available_cols = cols - 4;
        if (available_cols < 1) available_cols = 1;
        if (text_len > (size_t)available_cols) {
            size_t diff = text_len - available_cols;
            text = text + diff;
            text[0] = '.';
            text[1] = '.';
            text[2] = '.';
        }
        if (i == selected) {
            fprintf(tty, "> %s\n", text);
        } else {
            fprintf(tty, "  %s\n", text);
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
    if (!mem) {
        printf("Failed to allocate memory for string\n");
        exit(1);
    }
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
        int path_len = strlen(base_path) + strlen(entry->d_name) + 2;
        char *path = malloc(path_len);
        if (!path) {
            printf("Failed to allocate memory for path\n");
            exit(1);
        }
        snprintf(path, path_len, "%s/%s", base_path, entry->d_name);
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

void restore_terminal(int) {
    free_matched_item_list(&list);
    free(matched_list.items); // only free the pointer, not items - owned by list
    disable_raw_mode(tty_fd);
    exit_alternate_buffer(tty);
    show_cursor(tty);
    fflush(tty);
    fclose(tty);
    exit(0);
}

void open_terminal() {
    tty = fopen("/dev/tty", "r+");
    if (!tty) {
        printf("Failed to open /dev/tty\n");
        exit(1);
    }
    tty_fd = fileno(tty);
}

int main(int argc, char **argv) {
    signal(SIGINT, restore_terminal);

    char *prompt = "Query>";
    for (size_t i = 1; i < (size_t)argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            if (i + 1 >= (size_t)argc) {
                printf("Missing argument for -p\n");
                exit(1);
            }
            i++;
            prompt = argv[i];
        }
    }

    open_terminal();

    init_matched_item_list(&list);

    if (isatty(STDIN_FILENO)) {
        read_from_directory(&list, ".");
    } else {
        read_from_input_or_pipe(&list);
    }

    struct winsize w;
    ioctl(tty_fd, TIOCGWINSZ, &w);
    int rows = w.ws_row - 3;
    int cols = w.ws_col;
    char query[256] = {0};
    size_t len = 0;
    char c;
    size_t selected = 0;
    size_t offset = 0;

    enter_alternate_buffer(tty);
    enable_raw_mode(tty_fd);

    while (1) {
        clear_screen(tty);
        fprintf(tty, "%s %s\n", prompt, query);
        for (size_t i = 0; i < (size_t)cols; i++) {
            fprintf(tty, "—");
        }
        fprintf(tty, "\n");
        free(matched_list.items);
        matched_list = sort_matched_item_list(&list, query);
        print_matched_list_items(&matched_list, selected, rows, cols, offset);
        fprintf(tty, "\033[1;%zuH", strlen(prompt) + len + 2);
        fflush(tty);

        int read_result = read(tty_fd, &c, 1);
        if (read_result == -1) continue;
        if (read_result == 0) break;

        if (c == '\n') {
            if (matched_list.count <= 0) continue;
            disable_raw_mode(tty_fd);
            exit_alternate_buffer(tty);
            show_cursor(tty);
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
                    if (selected >= offset + (size_t)rows) offset++;
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
    disable_raw_mode(tty_fd);
    exit_alternate_buffer(tty);
    fclose(tty);

    return 0;
}
