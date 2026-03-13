#ifndef FS_H
#define FS_H
#include <stdint.h>
#include <stddef.h>
#define MAX_FILES 64
#define MAX_FILE_SIZE 512
typedef struct {
    char name[32];
    uint32_t size;
    uint32_t lba;
    uint32_t flags;
    int32_t  parent_index; 
    uint8_t  reserved[16];
} __attribute__((packed)) disk_fs_node_t;
void init_fs();
int fs_create_file(const char* name);
int fs_create_dir(const char* name);
int fs_change_dir(const char* name);
int fs_write_file(const char* name, const char* data);
int fs_read_file(const char* name, char* buffer, size_t max_len);
int fs_delete_file(const char* name);
int fs_move_file(const char* name, const char* target_dir);
void fs_list_dir();
void fs_sync();
void get_current_dir_name(char* buf);
#endif
