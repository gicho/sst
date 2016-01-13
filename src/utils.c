#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

void die(const char *format, ...) {
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    exit(EXIT_FAILURE);
}

gchar* find_next(gchar *data, gunichar c) {
    for (gchar *s = data; (s && (*s) != '\0'); s = g_utf8_next_char(s)) {
        if (g_utf8_get_char(s) == c) {
            return s;
        }
    }

    return NULL;
}

gchar* next_char_equals(gchar *data, gunichar c) {
    data = g_utf8_next_char(data);

    if (!data) {
        return NULL;
    }

    if ((*data) != '\0') {
        return NULL;
    }

    if (g_utf8_get_char(data) != c) {
        return NULL;
    }

    return data;
}

bool empty_string(gchar *data) {
    if (!data) {
        return true;
    }

    if ((*data) == '\0') {
        return true;
    }

    return false;
}
