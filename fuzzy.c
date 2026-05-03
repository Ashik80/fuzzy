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
    char *last_slash = strrchr(haystack, '/');
    // depth penalty
    int depth = 0;
    for (size_t i = 0; haystack[i] != '\0'; i++) {
        if (haystack[i] == '/')
            depth++;
    }
    score -= depth * 2;
    // last occurence reward
    while (haystack[hi] != '\0') {
        if (tolower(haystack[hi]) == tolower(needle[ni])) {
            if (match_started == 0) match_started = 1;
            // consecutive reward
            if (consecutive > 0) {
                score += consecutive * 3;
            }
            // first letter in a segment reward
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
            // not consecutive penalty
            if (match_started == 1) score--;
            consecutive = 0;
        }
        hi++;
    }
    if (ni == nlen) {
        const char *filename = last_slash != NULL ? last_slash + 1 : haystack;
        // check if needle matches somewhere in the filename portion alone
        size_t temp_ni = 0;
        for (size_t i = 0; filename[i] != '\0' && temp_ni < nlen; i++) {
            if (tolower(filename[i]) == tolower(needle[temp_ni])) temp_ni++;
        }
        if (temp_ni == nlen) score += 20;
    }
    return ni == nlen ? score : NO_MATCH;
}
