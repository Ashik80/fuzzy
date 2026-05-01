#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define NO_MATCH -100

const char * fuzzy_match(const char *haystack, const char *needle) {
    size_t ni = 0;
    size_t hi = 0;
    size_t nlen = strlen(needle);
    while (haystack[hi] != '\0') {
        if (tolower(haystack[hi]) == tolower(needle[ni])) {
            ni++;
        }
        hi++;
    }
    return ni == nlen ? haystack : NULL;
}

int fuzzy_score(const char *haystack, const char *needle) {
    int score = 0;
    int consecutive = 0;
    size_t ni = 0;
    size_t hi = 0;
    size_t nlen = strlen(needle);
    int match_started = 0;
    while (haystack[hi] != '\0') {
        if (tolower(haystack[hi]) == tolower(needle[ni])) {
            if (match_started == 0) match_started = 1;
            if (consecutive > 0) {
                score += consecutive * 3;
            }
            if (hi == 0
                    || haystack[hi - 1] == ' '
                    || haystack[hi - 1] == '/'
                    || haystack[hi - 1] == '_'
                    || haystack[hi - 1] == '-'
                    || haystack[hi - 1] == '.') {
                score += 5;
            }
            score++;
            consecutive++;
            ni++;
            if (ni == nlen) break;
        } else {
            if (match_started == 1) score--;
            consecutive = 0;
        }
        hi++;
    }
    return ni == nlen ? score : NO_MATCH;
}

typedef struct {
    char *text;
    int score;
} MatchedItem;

MatchedItem * add_score_to_item(char *text, int score) {
    MatchedItem *item = malloc(sizeof(MatchedItem));
    item->text = text;
    item->score = score;
    return item;
}

typedef struct {
    MatchedItem **items;
    size_t count;
    size_t size;
} MatchedItemList;

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

void print_matched_list_items(MatchedItemList *list) {
    for (size_t i = 0; i < list->count; i++) {
        MatchedItem *item = list->items[i];
        printf("%s: %d\n", item->text, item->score);
    }
}

int compare_match(const void *a, const void *b) {
    return (*(MatchedItem **)b)->score - (*(MatchedItem **)a)->score;
}

void sort_matched_item_list(MatchedItemList *list) {
    qsort(list->items, list->count, sizeof(MatchedItem *), compare_match);
}

void free_matched_item_list(MatchedItemList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
}

int main() {
    char *haystacks[] = {"Hello world!", "Another string", "Something"};
    size_t haylen = sizeof(haystacks) / sizeof(haystacks[0]);
    printf("Length of haystacks: %zu\n", haylen);

    printf("Items:\n");
    for (size_t i = 0; i < haylen; i++) {
        printf("%s\n", haystacks[i]);
    }

    printf("\n");

    printf("Enter a search string: ");

    char input[10];
    fgets(input, 10, stdin);
    size_t len = strlen(input);

    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }

    printf("\n");

    MatchedItemList list;
    init_matched_item_list(&list);

    for (size_t i = 0; i < haylen; i++) {
        char *haystack = haystacks[i];
        int score = fuzzy_score(haystack, input);
        if (score != NO_MATCH) {
            MatchedItem *item = add_score_to_item(haystack, score);
            add_matched_item_to_list(&list, item);
        }
    }

    sort_matched_item_list(&list);
    print_matched_list_items(&list);

    // if (fuzzy_match(haystack, input) != NULL) {
    //     printf("Matches\n");
    // } else {
    //     printf("Does not match\n");
    // }

    free_matched_item_list(&list);

    return 0;
}
