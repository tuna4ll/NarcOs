#ifndef EDITOR_H
#define EDITOR_H
void editor_start(const char* filename);
extern volatile int editor_running;
extern volatile int editor_input_key; 
extern volatile int editor_special_key;
#endif
