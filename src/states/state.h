#ifndef STATE_H
#define STATE_H

typedef struct State State;

struct State {
  int (*init)(void);
  void (*run)(void);
  void (*exit)(void);
};
 
#endif // !STATE_H
