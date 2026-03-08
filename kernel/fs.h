#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_FILES 8
#define MAX_FILE_SIZE 512

void init_fs();
int fs_create_file(const char* name);
int fs_write_file(const char* name, const char* data);
int fs_read_file(const char* name, char* buffer, size_t max_len);
int fs_delete_file(const char* name);
void fs_list_dir();

#endif // FS_H
