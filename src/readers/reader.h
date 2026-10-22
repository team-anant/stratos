#ifndef READER_H
#define READER_H

typedef struct Reader Reader;

struct Reader {
  int (*init)(void);
  void (*run)(void);
  void (*exit)(void);
};

#endif // !READER_H
