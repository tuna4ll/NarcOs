#include "fs.h"
#include "string.h"
#include "ata.h"
extern void vga_print(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vga_println(const char* str);
extern void vga_print_int(int num);
#define DIR_SECTOR 50
disk_fs_node_t dir_cache[MAX_FILES];
uint8_t sector_buffer[512];
int current_dir_index = -1;
void fs_sync() {
    for (int i = 0; i < 8; i++) {
        ata_write_sector(DIR_SECTOR + i, (uint8_t*)dir_cache + (i * 512));
    }
}
static void load_dir_cache() {
    for (int i = 0; i < 8; i++) {
        ata_read_sector(DIR_SECTOR + i, (uint8_t*)dir_cache + (i * 512));
    }
}
void init_fs() {
    load_dir_cache();
    int needs_format = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags > 2) {
            needs_format = 1;
            break;
        }
    }
    if (needs_format) {
        for (int i = 0; i < MAX_FILES; i++) {
            dir_cache[i].flags = 0;
            dir_cache[i].size = 0;
            dir_cache[i].parent_index = -1;
            dir_cache[i].name[0] = '\0';
        }
        // Standard Linux-like Structure
        fs_create_dir("bin");
        fs_create_dir("system");
        fs_create_dir("home");
        
        fs_change_dir("home");
        fs_create_dir("user");
        fs_change_dir("user");
        fs_create_dir("Desktop");
        fs_change_dir("Desktop");
        
        fs_create_file("readme.txt");
        fs_write_file("readme.txt", "Welcome to NarcOs Professional Desktop!\nFiles here appear on your desktop icons.\n");
        
        fs_change_dir("/"); // Back to root
        fs_sync();
    }
}
static int _fs_find_file(const char* name, uint32_t type) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags == type && dir_cache[i].parent_index == current_dir_index && strcmp(dir_cache[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}
int fs_create_file(const char* name) {
    if (_fs_find_file(name, 1) != -1) return -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags == 0) {
            strncpy(dir_cache[i].name, name, 31);
            dir_cache[i].name[31] = '\0';
            dir_cache[i].size = 0;
            dir_cache[i].flags = 1;
            dir_cache[i].parent_index = current_dir_index;
            dir_cache[i].lba = 100 + i;
            fs_sync();
            return 0;
        }
    }
    return -1;
}
int fs_create_dir(const char* name) {
    if (_fs_find_file(name, 2) != -1) return -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags == 0) {
            strncpy(dir_cache[i].name, name, 31);
            dir_cache[i].name[31] = '\0';
            dir_cache[i].size = 0;
            dir_cache[i].flags = 2;
            dir_cache[i].parent_index = current_dir_index;
            dir_cache[i].lba = 0;
            fs_sync();
            return 0;
        }
    }
    return -1;
}
int fs_change_dir(const char* name) {
    if (strcmp(name, "..") == 0) {
        if (current_dir_index != -1) {
            current_dir_index = dir_cache[current_dir_index].parent_index;
            return 0;
        }
        return -1;
    }
    if (strcmp(name, "/") == 0) {
        current_dir_index = -1;
        return 0;
    }
    int idx = _fs_find_file(name, 2);
    if (idx != -1) {
        current_dir_index = idx;
        return 0;
    }
    return -1;
}
int fs_write_file(const char* name, const char* data) {
    int idx = _fs_find_file(name, 1);
    if (idx == -1) {
        if (fs_create_file(name) == -1) return -1;
        idx = _fs_find_file(name, 1);
    }
    size_t len = strlen(data);
    if (len > 512) len = 512;
    dir_cache[idx].size = (uint32_t)len;
    fs_sync();
    for (int i = 0; i < 512; i++) sector_buffer[i] = 0;
    strncpy((char*)sector_buffer, data, len);
    ata_write_sector(dir_cache[idx].lba, sector_buffer);
    return 0;
}
int fs_read_file(const char* name, char* buffer, size_t max_len) {
    int idx = _fs_find_file(name, 1);
    if (idx == -1) return -1;
    ata_read_sector(dir_cache[idx].lba, sector_buffer);
    size_t read_len = dir_cache[idx].size;
    if (read_len >= max_len) read_len = max_len - 1;
    for (size_t i = 0; i < read_len; i++) {
        buffer[i] = sector_buffer[i];
    }
    buffer[read_len] = '\0';
    return 0;
}
int fs_delete_file(const char* name) {
    int idx = _fs_find_file(name, 1);
    if (idx == -1) idx = _fs_find_file(name, 2); // Also delete dirs
    if (idx == -1) return -1;
    dir_cache[idx].flags = 0;
    fs_sync();
    return 0;
}
int fs_move_file(const char* name, const char* target_dir) {
    int file_idx = _fs_find_file(name, 1);
    if (file_idx == -1) file_idx = _fs_find_file(name, 2);
    if (file_idx == -1) return -1;

    int target_idx = -1;
    
    if (strcmp(target_dir, "/") == 0) target_idx = -1;
    else {
        target_idx = _fs_find_file(target_dir, 2);
    }
    
    if (target_idx == -1 && strcmp(target_dir, "/") != 0) return -1;
    
    dir_cache[file_idx].parent_index = target_idx;
    fs_sync();
    return 0;
}
void fs_list_dir() {
    vga_print_color("Name\t\tSize (Bytes)\n", 0x07);
    vga_print_color("----------------------------\n", 0x08);
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == current_dir_index) {
            if (dir_cache[i].flags == 2) {
                vga_print_color(dir_cache[i].name, 0x0A);
                vga_println("/\t\t<DIR>");
            } else {
                vga_print_color(dir_cache[i].name, 0x0B);
                vga_print("\t");
                if (strlen(dir_cache[i].name) < 8) {
                    vga_print("\t");
                }
                vga_print_int(dir_cache[i].size);
                vga_println("");
            }
        }
    }
}
void get_current_dir_name(char* buf) {
    if (current_dir_index == -1) {
        buf[0] = '\0';
    } else {
        strncpy(buf, dir_cache[current_dir_index].name, 31);
        buf[31] = '\0';
    }
}
