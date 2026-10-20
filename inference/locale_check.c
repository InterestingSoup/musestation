/* Exercise the bounded native conversion algorithms against real host libc
 * conversion primitives. This does not execute the PS5 libc imports. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

int main(void) {
    assert(setlocale(LC_ALL, "C"));
    const char *source = "hello";
    wchar_t wide[8] = {0};
    mbstate_t state = {0};
    assert(mbsnrtowcs(wide, &source, 5, 2, &state) == 2);
    assert(wide[0] == L'h' && wide[1] == L'e' && !strcmp(source, "llo"));
    assert(mbsnrtowcs(wide + 2, &source, 4, 6, &state) == 3 && source == NULL);
    assert(!wcscmp(wide, L"hello"));
    source = "hello";
    const char *saved = source;
    assert(mbsnrtowcs(NULL, &source, 6, 0, NULL) == 5 && source == saved);
    source = "\xff";
    errno = 0;
    assert(mbsnrtowcs(wide, &source, 1, 8, NULL) == (size_t)-1 && errno == EILSEQ);

    const wchar_t *words = L"hello";
    char bytes[8] = {0};
    memset(&state, 0, sizeof(state));
    assert(wcsnrtombs(bytes, &words, 6, 2, &state) == 2);
    assert(bytes[0] == 'h' && bytes[1] == 'e' && !wcscmp(words, L"llo"));
    assert(wcsnrtombs(bytes + 2, &words, 4, 6, &state) == 3 && words == NULL);
    assert(!strcmp(bytes, "hello"));
    words = L"hello";
    const wchar_t *saved_words = words;
    assert(wcsnrtombs(NULL, &words, 6, 0, NULL) == 5 && words == saved_words);
    words = L"\u20ac";
    errno = 0;
    assert(wcsnrtombs(bytes, &words, 1, 8, NULL) == (size_t)-1 && errno == EILSEQ);
    puts("Bounded multibyte conversion checks passed (Linux Docker C locale)");
    return 0;
}
