#ifndef READER_TABLE_H
#define READER_TABLE_H

#include "readers/reader.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

// DO NOT CHANGE !!!!
// It is the max amount of bits a reader can affect using
// the 32 bit signal payload it sends (divided in two for
// the two masks it needs to send).
#define MAX_AFFECTED_BITS 16

#define NO_OF_READERS 1

struct ReaderEntry {
  pid_t pid;
  const Reader *reader;
  const size_t scope_len;                  // how many entries of scope are valid
  const uint8_t scope[MAX_AFFECTED_BITS]; // the index of the bits the reader affects
};

extern struct ReaderEntry reader_table[NO_OF_READERS];

#endif // !READER_TABLE_H
