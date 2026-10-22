#include "reader_table.h"
extern const Reader mock;

struct ReaderEntry reader_table[NO_OF_READERS] = {
    {.reader = &mock, .scope_len = 2, .scope = {0, 1}},
};
