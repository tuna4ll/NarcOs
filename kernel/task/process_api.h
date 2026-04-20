#ifndef PROCESS_API_H
#define PROCESS_API_H

#include <stdint.h>

#define PROCESS_SNAPSHOT_NAME_LEN 32
#define PROCESS_SNAPSHOT_IMAGE_LEN 128

typedef struct {
    int pid;
    int parent_pid;
    int state;
    int kind;
    int exit_code;
    uint32_t flags;
    char name[PROCESS_SNAPSHOT_NAME_LEN];
    char image_path[PROCESS_SNAPSHOT_IMAGE_LEN];
} process_snapshot_entry_t;

#endif
