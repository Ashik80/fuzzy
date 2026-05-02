#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include "fuzzy.h"

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
