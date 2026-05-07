#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <pthread.h>
#include "fuzzy.h"
#include "term_escapes.h"
#include "term_mode.h"

#define FRAME_SIZE 234234
#define INITIAL_MATCH_LIST_SIZE 10

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
size_t files_traversed = 0;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t data_ready = PTHREAD_COND_INITIALIZER;
pthread_t loader;
int loader_started = 0;
int has_data = 0;

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
    list->size = INITIAL_MATCH_LIST_SIZE;
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

void print_matched_list_items(
        MatchedItemList *list,
        const size_t selected,
        const int rows,
        const int cols,
        const size_t offset,
        char *frame,
        int *frame_len) {
    for (size_t i = offset; i < list->count && i < offset + rows; i++) {
        MatchedItem *item = list->items[i];
        size_t text_len = strlen(item->text);
        char *text = copy_string(item->text);
        char *orig_text_p = text;
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
            *frame_len += snprintf(frame + *frame_len, FRAME_SIZE - *frame_len, "\033[1m> %s\033[0m\n", text);
        } else {
            *frame_len += snprintf(frame + *frame_len, FRAME_SIZE - *frame_len, "  %s\n", text);
        }
        free(orig_text_p);
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

void *load_from_pipe(void *argv) {
    int fd = *(int *)argv;
    free(argv);
    FILE *pipe = fdopen(fd, "r");
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        size_t len = strlen(buffer);
        if (buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        MatchedItem *item = add_score_to_item(copy_string(buffer), 0);
        pthread_mutex_lock(&list_mutex);
        add_matched_item_to_list(&list, item);
        if (!has_data) {
            has_data = 1;
            pthread_cond_signal(&data_ready);
        }
        pthread_mutex_unlock(&list_mutex);
    }
    fclose(pipe);
    return NULL;
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
    if (loader) {
        pthread_cancel(loader);
        pthread_join(loader, NULL);
    }
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

    init_matched_item_list(&list);

    if (isatty(STDIN_FILENO)) {
        open_terminal();
        read_from_directory(&list, ".");
    } else {
        int *fd = malloc(sizeof(int));
        *fd = dup(STDIN_FILENO);
        open_terminal();
        pthread_create(&loader, NULL, load_from_pipe, fd);
        loader_started = 1;
    }

    struct winsize w;
    ioctl(tty_fd, TIOCGWINSZ, &w);
    int rows = w.ws_row - 3;
    int cols = w.ws_col;
    char query[256] = {0};
    size_t len = 0;
    size_t cursor = 0;
    char c;
    size_t selected = 0;
    size_t offset = 0;
    char frame[FRAME_SIZE];
    int frame_len = 0;

    enter_alternate_buffer(tty);
    enable_raw_mode(tty_fd);

    if (loader_started) {
        pthread_mutex_lock(&list_mutex);
        while (!has_data) {
            pthread_cond_wait(&data_ready, &list_mutex);
        }
        pthread_mutex_unlock(&list_mutex);
    }

    while (1) {
        frame_len = 0;
        clear_screen(tty);
        frame_len += snprintf(frame + frame_len, FRAME_SIZE - frame_len, "%s %s\n", prompt, query);
        for (size_t i = 0; i < (size_t)cols; i++) {
            frame_len += snprintf(frame + frame_len, FRAME_SIZE - frame_len, "—");
        }
        frame_len += snprintf(frame + frame_len, FRAME_SIZE - frame_len, "\n");
        free(matched_list.items);
        pthread_mutex_lock(&list_mutex);
        matched_list = sort_matched_item_list(&list, query);
        pthread_mutex_unlock(&list_mutex);
        print_matched_list_items(&matched_list, selected, rows, cols, offset, frame, &frame_len);
        frame_len += snprintf(frame + frame_len, FRAME_SIZE - frame_len, "\033[1;%zuH", strlen(prompt) + cursor + 2);
        fwrite(frame, 1, frame_len, tty);
        fflush(tty);

        int read_result = read(tty_fd, &c, 1);
        if (read_result == -1) continue;
        if (read_result == 0) break;

        if (c == '\n' || c == 0x19) { // enter or ctrl-y
            if (matched_list.count <= 0) continue;
            if (loader) {
                pthread_cancel(loader);
                pthread_join(loader, NULL);
            }
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
                if (seq[1] == 'A') { // up arrow
                    if (selected > 0) selected--;
                    if (selected < offset) offset--;
                } else if (seq[1] == 'B') { // down arrow
                    if (matched_list.count > 0 && selected < matched_list.count - 1) selected++;
                    if (selected >= offset + (size_t)rows) offset++;
                } else if (seq[1] == 'C') { // right arrow
                    if (cursor < len) cursor++;
                } else if (seq[1] == 'D') { // left arrow
                    if (cursor > 0) cursor--;
                }
            }
            continue;
        }
        if (c == 0x01) { // ctrl-a
            cursor = 0;
        } else if (c == 0x05) { // ctrl-e
            cursor = len;
        } else if (c == 0x02) { // ctrl-b
            if (cursor > 0) cursor--;
        } else if (c == 0x06) { // ctrl-f
            if (cursor < len) cursor++;
        } else if (c == 0x0E) { // ctrl-n
            if (matched_list.count > 0 && selected < matched_list.count - 1) selected++;
            if (selected >= offset + (size_t)rows) offset++;
        } else if (c == 0x10) { // ctrl-p
            if (selected > 0) selected--;
            if (selected < offset) offset--;
        } else if (c == 0x15) { // ctrl-u
            if (cursor > 0) {
                memmove(&query[0], &query[cursor], len - cursor + 1);
                len = len - cursor;
                cursor = 0;
                query[len] = '\0';
                selected = 0;
                offset = 0;
            }
        } else if (c == 0x0B) { // ctrl-k
            if (cursor < len) {
                query[cursor] = '\0';
                len = cursor;
                selected = 0;
                offset = 0;
            }
        } else if (c == 127) { // backspace
            if (cursor > 0) {
                memmove(&query[cursor - 1], &query[cursor], len - cursor + 1);
                len--;
                cursor--;
                query[len] = '\0';
                selected = 0;
                offset = 0;
            }
        } else { // any other character
            if (len < sizeof(query) - 1) {
                memmove(&query[cursor + 1], &query[cursor], len - cursor + 1);
                query[cursor] = c;
                len++;
                cursor++;
                query[len] = '\0';
                selected = 0;
                offset = 0;
            }
        }
    }

    if (loader) {
        pthread_cancel(loader);
        pthread_join(loader, NULL);
    }
    free_matched_item_list(&list);
    free(matched_list.items);
    disable_raw_mode(tty_fd);
    exit_alternate_buffer(tty);
    fclose(tty);

    return 0;
}
