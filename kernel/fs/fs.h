#ifndef FS_H
#define FS_H
#include <stdint.h>
#include <stddef.h>
#define MAX_FILES 64
#define MAX_FILE_SIZE 4096
#define FS_NODE_FILE 1
#define FS_NODE_DIR  2
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
int fs_write_file_by_idx(int idx, const char* data);
int fs_read_file_by_idx(int idx, char* buffer, size_t max_len);
int fs_write_file(const char* name, const char* data);
int fs_read_file(const char* name, char* buffer, size_t max_len);
int fs_delete_file(const char* name);
int fs_move_file(const char* name, const char* target_dir);
int fs_rename(const char* path, const char* new_name);
void fs_list_dir();
void fs_sync();
void get_current_dir_name(char* buf);
void fs_get_current_path(char* buf, size_t max_len);
int fs_find_node(const char* path);
int fs_list_dir_entries(disk_fs_node_t* out_entries, int max_entries);
int fs_get_node_info(int idx, disk_fs_node_t* out_node);
void fs_get_path_by_index(int idx, char* buf, size_t max_len);
#endif
