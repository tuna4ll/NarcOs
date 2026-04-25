#include "fs.h"
#include "string.h"
#include "storage.h"
#include "memory_alloc.h"
#include "serial.h"
extern void vga_print(const char* str);
extern void vga_print_color(const char* str, uint8_t color);
extern void vga_println(const char* str);
extern void vga_print_int(int num);
#define DIR_SECTOR 2048
#define DIR_SECTOR_COUNT 8
#define DATA_START_SECTOR 4096
#define DATA_END_SECTOR 32768
#define FS_ROOT_INDEX (-1)
#define FS_INVALID_INDEX (-2)
disk_fs_node_t dir_cache[MAX_FILES];
uint8_t sector_buffer[512];
int current_dir_index = -1;

#if UINTPTR_MAX > 0xFFFFFFFFU
extern const uint8_t _binary_obj_x86_64_user_bin_hello_start[];
extern const uint8_t _binary_obj_x86_64_user_bin_hello_end[];
extern const uint8_t _binary_obj_x86_64_user_bin_ps_start[];
extern const uint8_t _binary_obj_x86_64_user_bin_ps_end[];
extern const uint8_t _binary_obj_x86_64_user_bin_cat_start[];
extern const uint8_t _binary_obj_x86_64_user_bin_cat_end[];
extern const uint8_t _binary_obj_x86_64_user_bin_echo_start[];
extern const uint8_t _binary_obj_x86_64_user_bin_echo_end[];
extern const uint8_t _binary_obj_x86_64_user_bin_kill_start[];
extern const uint8_t _binary_obj_x86_64_user_bin_kill_end[];
extern const uint8_t _binary_obj_x86_64_user_bin_proc_test_start[];
extern const uint8_t _binary_obj_x86_64_user_bin_proc_test_end[];
extern const uint8_t _binary_obj_x86_64_user_bin_pipe_test_start[];
extern const uint8_t _binary_obj_x86_64_user_bin_pipe_test_end[];
#else
extern const uint8_t _binary_obj_i386_user_bin_hello_start[];
extern const uint8_t _binary_obj_i386_user_bin_hello_end[];
extern const uint8_t _binary_obj_i386_user_bin_ps_start[];
extern const uint8_t _binary_obj_i386_user_bin_ps_end[];
extern const uint8_t _binary_obj_i386_user_bin_cat_start[];
extern const uint8_t _binary_obj_i386_user_bin_cat_end[];
extern const uint8_t _binary_obj_i386_user_bin_echo_start[];
extern const uint8_t _binary_obj_i386_user_bin_echo_end[];
extern const uint8_t _binary_obj_i386_user_bin_kill_start[];
extern const uint8_t _binary_obj_i386_user_bin_kill_end[];
extern const uint8_t _binary_obj_i386_user_bin_proc_test_start[];
extern const uint8_t _binary_obj_i386_user_bin_proc_test_end[];
extern const uint8_t _binary_obj_i386_user_bin_pipe_test_start[];
extern const uint8_t _binary_obj_i386_user_bin_pipe_test_end[];
#endif

typedef struct {
    const char* path;
    const uint8_t* start;
    const uint8_t* end;
} fs_packaged_binary_t;

static const fs_packaged_binary_t fs_packaged_binaries[] = {
#if UINTPTR_MAX > 0xFFFFFFFFU
    { "/bin/hello", _binary_obj_x86_64_user_bin_hello_start, _binary_obj_x86_64_user_bin_hello_end },
    { "/bin/ps", _binary_obj_x86_64_user_bin_ps_start, _binary_obj_x86_64_user_bin_ps_end },
    { "/bin/cat", _binary_obj_x86_64_user_bin_cat_start, _binary_obj_x86_64_user_bin_cat_end },
    { "/bin/echo", _binary_obj_x86_64_user_bin_echo_start, _binary_obj_x86_64_user_bin_echo_end },
    { "/bin/kill", _binary_obj_x86_64_user_bin_kill_start, _binary_obj_x86_64_user_bin_kill_end },
    { "/bin/proc_test", _binary_obj_x86_64_user_bin_proc_test_start, _binary_obj_x86_64_user_bin_proc_test_end },
    { "/bin/pipe_test", _binary_obj_x86_64_user_bin_pipe_test_start, _binary_obj_x86_64_user_bin_pipe_test_end }
#else
    { "/bin/hello", _binary_obj_i386_user_bin_hello_start, _binary_obj_i386_user_bin_hello_end },
    { "/bin/ps", _binary_obj_i386_user_bin_ps_start, _binary_obj_i386_user_bin_ps_end },
    { "/bin/cat", _binary_obj_i386_user_bin_cat_start, _binary_obj_i386_user_bin_cat_end },
    { "/bin/echo", _binary_obj_i386_user_bin_echo_start, _binary_obj_i386_user_bin_echo_end },
    { "/bin/kill", _binary_obj_i386_user_bin_kill_start, _binary_obj_i386_user_bin_kill_end },
    { "/bin/proc_test", _binary_obj_i386_user_bin_proc_test_start, _binary_obj_i386_user_bin_proc_test_end },
    { "/bin/pipe_test", _binary_obj_i386_user_bin_pipe_test_start, _binary_obj_i386_user_bin_pipe_test_end }
#endif
};

static uint32_t node_sector_count(const disk_fs_node_t* node) {
    uint32_t count;
    memcpy(&count, node->reserved, sizeof(count));
    if (count == 0 && node->flags == 1 && node->size > 0) count = 1;
    return count;
}

static void set_node_sector_count(disk_fs_node_t* node, uint32_t count) {
    memcpy(node->reserved, &count, sizeof(count));
}

static int fs_blob_matches_file(const char* path, const uint8_t* data, size_t len) {
    uint8_t verify_buffer[512];
    size_t offset = 0;
    int idx = fs_find_node(path);

    if (idx < 0 || dir_cache[idx].flags != FS_NODE_FILE) return 0;
    if (dir_cache[idx].size != len) return 0;

    while (offset < len) {
        size_t chunk = len - offset;
        int read_status;

        if (chunk > sizeof(verify_buffer)) chunk = sizeof(verify_buffer);
        read_status = fs_read_file_raw_by_idx(idx, verify_buffer, offset, chunk);
        if (read_status != (int)chunk) return 0;
        if (memcmp(verify_buffer, data + offset, chunk) != 0) return 0;
        offset += chunk;
    }
    return 1;
}

static void fs_sync_packaged_binaries() {
    size_t count = sizeof(fs_packaged_binaries) / sizeof(fs_packaged_binaries[0]);

    (void)fs_create_dir("/bin");
    for (size_t i = 0; i < count; i++) {
        const fs_packaged_binary_t* entry = &fs_packaged_binaries[i];
        size_t len = (size_t)(entry->end - entry->start);
        int status;

        if (len == 0U) continue;
        if (fs_blob_matches_file(entry->path, entry->start, len)) continue;
        status = fs_write_file_raw(entry->path, entry->start, len);
        if (status < 0) {
            serial_write("[fs] packaged sync failed path=");
            serial_write(entry->path);
            serial_write(" len=");
            serial_write_hex32((uint32_t)len);
            serial_write(" status=");
            serial_write_hex32((uint32_t)status);
            serial_write_char('\n');
        }
    }
}

static int fs_find_child(int parent_idx, const char* name, uint32_t type) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags == 0) continue;
        if (dir_cache[i].parent_index != parent_idx) continue;
        if (type != 0 && dir_cache[i].flags != type) continue;
        if (strcmp(dir_cache[i].name, name) == 0) return i;
    }
    return -1;
}

static int fs_is_valid_name(const char* name) {
    if (!name || name[0] == '\0') return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (int i = 0; name[i] != '\0'; i++) {
        if (name[i] == '/') return 0;
    }
    return 1;
}

static int fs_dir_has_children(int idx) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags != 0 && dir_cache[i].parent_index == idx) return 1;
    }
    return 0;
}

static int fs_walk_path(const char* path, int want_parent, char* leaf_out) {
    int node = (path && path[0] == '/') ? FS_ROOT_INDEX : current_dir_index;
    int i = (path && path[0] == '/') ? 1 : 0;
    char part[32];

    if (!path || path[0] == '\0') return node;

    while (1) {
        int j = 0;
        while (path[i] == '/') i++;
        if (path[i] == '\0') {
            if (want_parent && leaf_out) leaf_out[0] = '\0';
            return node;
        }
        while (path[i] != '\0' && path[i] != '/' && j < 31) {
            part[j++] = path[i++];
        }
        part[j] = '\0';
        while (path[i] != '\0' && path[i] != '/') i++;
        while (path[i] == '/') i++;

        if (strcmp(part, ".") == 0 || part[0] == '\0') {
            continue;
        }
        if (strcmp(part, "..") == 0) {
            if (node != FS_ROOT_INDEX) node = dir_cache[node].parent_index;
            continue;
        }
        if (path[i] == '\0' && want_parent) {
            if (leaf_out) strcpy(leaf_out, part);
            return node;
        }
        node = fs_find_child(node, part, 2);
        if (node == -1) return FS_INVALID_INDEX;
    }
}

static void fs_format() {
    for (int i = 0; i < MAX_FILES; i++) {
        dir_cache[i].flags = 0;
        dir_cache[i].size = 0;
        dir_cache[i].parent_index = FS_ROOT_INDEX;
        dir_cache[i].lba = 0;
        dir_cache[i].name[0] = '\0';
        memset(dir_cache[i].reserved, 0, sizeof(dir_cache[i].reserved));
    }

    current_dir_index = FS_ROOT_INDEX;
    fs_create_dir("/bin");
    fs_create_dir("/system");
    fs_create_dir("/home");
    fs_create_dir("/home/user");
    fs_create_dir("/home/user/Desktop");
    fs_write_file("/home/user/Desktop/readme.txt",
                  "Welcome to NarcOs Professional Desktop!\n"
                  "Files here appear on your desktop icons.\n");
    current_dir_index = FS_ROOT_INDEX;
    fs_sync();
}

static int fs_validate() {
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags == 0) continue;
        if (dir_cache[i].flags != FS_NODE_FILE && dir_cache[i].flags != FS_NODE_DIR) return 0;
        if (!fs_is_valid_name(dir_cache[i].name)) return 0;
        if (dir_cache[i].parent_index < FS_ROOT_INDEX || dir_cache[i].parent_index >= MAX_FILES) return 0;
        if (dir_cache[i].parent_index >= 0 && dir_cache[dir_cache[i].parent_index].flags != FS_NODE_DIR) return 0;
        if (dir_cache[i].flags == FS_NODE_FILE) {
            uint32_t sectors = node_sector_count(&dir_cache[i]);
            if (dir_cache[i].size > MAX_FILE_SIZE) return 0;
            if (dir_cache[i].size == 0 && sectors != 0) return 0;
            if (dir_cache[i].size > 0 && sectors == 0) return 0;
            if (dir_cache[i].lba != 0 && (dir_cache[i].lba < DATA_START_SECTOR || dir_cache[i].lba + sectors > DATA_END_SECTOR)) {
                return 0;
            }
        }
        int slow = i;
        while (slow >= 0) {
            slow = dir_cache[slow].parent_index;
            if (slow == i) return 0;
        }
        for (int j = i + 1; j < MAX_FILES; j++) {
            if (dir_cache[j].flags == 0) continue;
            if (dir_cache[j].parent_index == dir_cache[i].parent_index && strcmp(dir_cache[j].name, dir_cache[i].name) == 0) {
                return 0;
            }
        }
    }

    if (fs_find_child(FS_ROOT_INDEX, "home", FS_NODE_DIR) == -1) return 0;
    if (fs_find_node("/home/user/Desktop") == -1) return 0;
    return 1;
}

static int fs_alloc_data_run(uint32_t sectors, int ignore_idx) {
    if (sectors == 0) return 0;
    uint32_t run = 0;
    uint32_t start = DATA_START_SECTOR;
    for (uint32_t lba = DATA_START_SECTOR; lba < DATA_END_SECTOR; lba++) {
        int used = 0;
        for (int i = 0; i < MAX_FILES; i++) {
            if (i == ignore_idx || dir_cache[i].flags != 1) continue;
            uint32_t node_lba = dir_cache[i].lba;
            uint32_t node_secs = node_sector_count(&dir_cache[i]);
            if (node_secs == 0) continue;
            if (lba >= node_lba && lba < node_lba + node_secs) {
                used = 1;
                break;
            }
        }
        if (!used) {
            if (run == 0) start = lba;
            run++;
            if (run == sectors) return (int)start;
        } else {
            run = 0;
        }
    }
    return -1;
}

static void fs_zero_sectors(uint32_t lba, uint32_t count) {
    memset(sector_buffer, 0, sizeof(sector_buffer));
    for (uint32_t i = 0; i < count; i++) {
        (void)storage_write_sector(lba + i, sector_buffer);
    }
}

void fs_sync() {
    for (int i = 0; i < DIR_SECTOR_COUNT; i++) {
        (void)storage_write_sector(DIR_SECTOR + (uint32_t)i, (uint8_t*)dir_cache + (i * 512));
    }
}
static void load_dir_cache() {
    for (int i = 0; i < DIR_SECTOR_COUNT; i++) {
        if (storage_read_sector(DIR_SECTOR + (uint32_t)i, (uint8_t*)dir_cache + (i * 512)) != 0) {
            memset((uint8_t*)dir_cache + (i * 512), 0, 512);
        }
    }
}
void init_fs() {
#if UINTPTR_MAX > 0xFFFFFFFFU
    memset(dir_cache, 0, sizeof(dir_cache));
    current_dir_index = FS_ROOT_INDEX;
    fs_format();
    serial_write("[fs] post-format /bin=");
    serial_write_hex32((uint32_t)fs_find_node("/bin"));
    serial_write(" /home=");
    serial_write_hex32((uint32_t)fs_find_node("/home"));
    serial_write(" /home/user/Desktop=");
    serial_write_hex32((uint32_t)fs_find_node("/home/user/Desktop"));
    serial_write_char('\n');
    fs_sync_packaged_binaries();
    return;
#endif

    load_dir_cache();
    current_dir_index = FS_ROOT_INDEX;
    if (!fs_validate()) fs_format();
    fs_sync_packaged_binaries();
}
int fs_create_file(const char* name) {
    char leaf[32];
    leaf[0] = '\0';
    int parent = fs_walk_path(name, 1, leaf);
    if (parent == FS_INVALID_INDEX || leaf[0] == '\0' || !fs_is_valid_name(leaf)) {
        serial_write("[fs] create_file invalid path=");
        serial_write(name ? name : "<null>");
        serial_write(" parent=");
        serial_write_hex32((uint32_t)parent);
        serial_write(" leaf=");
        serial_write(leaf);
        serial_write_char('\n');
        return -1;
    }
    if (fs_find_child(parent, leaf, 0) != -1) {
        serial_write("[fs] create_file exists path=");
        serial_write(name ? name : "<null>");
        serial_write(" parent=");
        serial_write_hex32((uint32_t)parent);
        serial_write(" leaf=");
        serial_write(leaf);
        serial_write_char('\n');
        return -1;
    }
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags == 0) {
            strncpy(dir_cache[i].name, leaf, 31);
            dir_cache[i].name[31] = '\0';
            dir_cache[i].size = 0;
            dir_cache[i].flags = FS_NODE_FILE;
            dir_cache[i].parent_index = parent;
            dir_cache[i].lba = 0;
            memset(dir_cache[i].reserved, 0, sizeof(dir_cache[i].reserved));
            fs_sync();
            return 0;
        }
    }
    serial_write("[fs] create_file full path=");
    serial_write(name ? name : "<null>");
    serial_write_char('\n');
    return -1;
}
int fs_create_dir(const char* name) {
    char leaf[32];
    leaf[0] = '\0';
    int parent = fs_walk_path(name, 1, leaf);
    if (parent == FS_INVALID_INDEX || leaf[0] == '\0' || !fs_is_valid_name(leaf)) {
        serial_write("[fs] create_dir invalid path=");
        serial_write(name ? name : "<null>");
        serial_write(" parent=");
        serial_write_hex32((uint32_t)parent);
        serial_write(" leaf=");
        serial_write(leaf);
        serial_write_char('\n');
        return -1;
    }
    if (fs_find_child(parent, leaf, 0) != -1) {
        serial_write("[fs] create_dir exists path=");
        serial_write(name ? name : "<null>");
        serial_write(" parent=");
        serial_write_hex32((uint32_t)parent);
        serial_write(" leaf=");
        serial_write(leaf);
        serial_write_char('\n');
        return -1;
    }
    for (int i = 0; i < MAX_FILES; i++) {
        if (dir_cache[i].flags == 0) {
            strncpy(dir_cache[i].name, leaf, 31);
            dir_cache[i].name[31] = '\0';
            dir_cache[i].size = 0;
            dir_cache[i].flags = FS_NODE_DIR;
            dir_cache[i].parent_index = parent;
            dir_cache[i].lba = 0;
            memset(dir_cache[i].reserved, 0, sizeof(dir_cache[i].reserved));
            fs_sync();
            return 0;
        }
    }
    serial_write("[fs] create_dir full path=");
    serial_write(name ? name : "<null>");
    serial_write_char('\n');
    return -1;
}
int fs_change_dir(const char* name) {
    int idx = fs_walk_path(name, 0, 0);
    if (idx != FS_INVALID_INDEX) {
        current_dir_index = idx;
        return 0;
    }
    return -1;
}
int fs_write_file_raw_by_idx(int idx, const void* data, size_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t needed_sectors;
    uint32_t current_sectors;
    uint32_t old_lba;
    int moved = 0;

    if (idx < 0 || idx >= MAX_FILES || dir_cache[idx].flags != FS_NODE_FILE) return -1;
    if (!bytes && len != 0U) return -1;
    if (len > MAX_FILE_SIZE) len = MAX_FILE_SIZE;
    current_sectors = node_sector_count(&dir_cache[idx]);
    old_lba = dir_cache[idx].lba;
    needed_sectors = (uint32_t)((len + 511U) / 512U);

    if (needed_sectors != 0) {
        if (dir_cache[idx].lba == 0 || current_sectors < needed_sectors) {
            int new_lba = fs_alloc_data_run(needed_sectors, idx);
            if (new_lba == -1) return -1;
            dir_cache[idx].lba = (uint32_t)new_lba;
            moved = (old_lba != 0 && old_lba != dir_cache[idx].lba);
        }
    } else {
        dir_cache[idx].lba = 0;
    }

    dir_cache[idx].size = (uint32_t)len;
    set_node_sector_count(&dir_cache[idx], needed_sectors);
    fs_sync();
    if (needed_sectors == 0) {
        if (old_lba != 0 && current_sectors != 0) fs_zero_sectors(old_lba, current_sectors);
        return 0;
    }

    for (uint32_t sector = 0; sector < needed_sectors; sector++) {
        size_t offset = sector * 512U;
        size_t chunk = len > offset ? len - offset : 0;
        if (chunk > 512U) chunk = 512U;
        memset(sector_buffer, 0, sizeof(sector_buffer));
        memcpy(sector_buffer, bytes + offset, chunk);
        if (storage_write_sector(dir_cache[idx].lba + sector, sector_buffer) != 0) return -1;
    }
    if (current_sectors > needed_sectors && dir_cache[idx].lba != 0) {
        fs_zero_sectors(dir_cache[idx].lba + needed_sectors, current_sectors - needed_sectors);
    }
    if (moved && old_lba != 0) fs_zero_sectors(old_lba, current_sectors);
    return (int)len;
}

int fs_write_file_by_idx(int idx, const char* data) {
    size_t len;

    if (idx < 0 || idx >= MAX_FILES || dir_cache[idx].flags != FS_NODE_FILE || !data) return -1;
    len = strlen(data);
    if (len > MAX_FILE_SIZE) len = MAX_FILE_SIZE;
    return fs_write_file_raw_by_idx(idx, data, len) < 0 ? -1 : 0;
}
int fs_write_file_raw_at_by_idx(int idx, const void* data, size_t offset, size_t len) {
    uint8_t* merged;
    size_t old_size;
    size_t new_size;
    int read_status;
    int write_status;

    if (idx < 0 || idx >= MAX_FILES || dir_cache[idx].flags != FS_NODE_FILE) return -1;
    if (!data && len != 0U) return -1;
    if (offset > MAX_FILE_SIZE) return -1;
    if (len > MAX_FILE_SIZE - offset) len = MAX_FILE_SIZE - offset;
    if (len == 0U) return 0;
    old_size = dir_cache[idx].size;
    new_size = old_size > offset + len ? old_size : offset + len;
    if (new_size == 0U) return fs_write_file_raw_by_idx(idx, 0, 0U);

    merged = (uint8_t*)malloc(new_size);
    if (!merged) return -1;
    memset(merged, 0, new_size);
    if (old_size != 0U) {
        read_status = fs_read_file_raw_by_idx(idx, merged, 0U, old_size);
        if (read_status < 0) {
            free(merged);
            return -1;
        }
    }
    if (len != 0U) memcpy(merged + offset, data, len);
    write_status = fs_write_file_raw_by_idx(idx, merged, new_size);
    free(merged);
    return write_status;
}
int fs_write_file(const char* name, const char* data) {
    int idx = fs_find_node(name);
    if (idx == -1) {
        if (fs_create_file(name) == -1) return -1;
        idx = fs_find_node(name);
    }
    if (idx == -1 || dir_cache[idx].flags != FS_NODE_FILE) return -1;
    return fs_write_file_by_idx(idx, data);
}
int fs_read_file_raw_by_idx(int idx, void* buffer, size_t offset, size_t max_len) {
    uint8_t* bytes = (uint8_t*)buffer;
    size_t read_len;
    uint32_t sectors;
    size_t copied = 0;
    uint32_t start_sector;
    size_t sector_offset;

    if (idx < 0 || idx >= MAX_FILES || dir_cache[idx].flags != FS_NODE_FILE) return -1;
    if (!bytes && max_len != 0U) return -1;
    if (offset >= dir_cache[idx].size || max_len == 0U) return 0;

    sectors = node_sector_count(&dir_cache[idx]);
    read_len = dir_cache[idx].size - offset;
    if (read_len > max_len) read_len = max_len;
    start_sector = (uint32_t)(offset / 512U);
    sector_offset = offset % 512U;

    for (uint32_t sector = start_sector; sector < sectors && copied < read_len; sector++) {
        size_t chunk;

        if (storage_read_sector(dir_cache[idx].lba + sector, sector_buffer) != 0) return -1;
        chunk = 512U - sector_offset;
        if (chunk > read_len - copied) chunk = read_len - copied;
        memcpy(bytes + copied, sector_buffer + sector_offset, chunk);
        copied += chunk;
        sector_offset = 0U;
    }

    return (int)copied;
}
int fs_read_file_by_idx(int idx, char* buffer, size_t max_len) {
    int read_len;

    if (idx < 0 || idx >= MAX_FILES || dir_cache[idx].flags != FS_NODE_FILE || !buffer || max_len == 0) return -1;
    read_len = fs_read_file_raw_by_idx(idx, buffer, 0U, max_len - 1U);
    if (read_len < 0) return -1;
    buffer[read_len] = '\0';
    return 0;
}
int fs_write_file_raw(const char* name, const void* data, size_t len) {
    int idx = fs_find_node(name);
    int status;

    if (idx == -1) {
        if (fs_create_file(name) == -1) {
            serial_write("[fs] write_raw create failed path=");
            serial_write(name ? name : "<null>");
            serial_write_char('\n');
            return -1;
        }
        idx = fs_find_node(name);
    }
    if (idx == -1 || dir_cache[idx].flags != FS_NODE_FILE) {
        serial_write("[fs] write_raw lookup failed path=");
        serial_write(name ? name : "<null>");
        serial_write(" idx=");
        serial_write_hex32((uint32_t)idx);
        serial_write(" flags=");
        serial_write_hex32((uint32_t)(idx >= 0 ? dir_cache[idx].flags : 0));
        serial_write_char('\n');
        return -1;
    }
    status = fs_write_file_raw_by_idx(idx, data, len);
    if (status < 0) {
        serial_write("[fs] write_raw body failed path=");
        serial_write(name ? name : "<null>");
        serial_write(" idx=");
        serial_write_hex32((uint32_t)idx);
        serial_write(" len=");
        serial_write_hex32((uint32_t)len);
        serial_write_char('\n');
    }
    return status;
}
int fs_write_file_raw_at(const char* name, const void* data, size_t offset, size_t len) {
    int idx = fs_find_node(name);

    if (idx == -1) {
        if (fs_create_file(name) == -1) return -1;
        idx = fs_find_node(name);
    }
    if (idx == -1 || dir_cache[idx].flags != FS_NODE_FILE) return -1;
    return fs_write_file_raw_at_by_idx(idx, data, offset, len);
}
int fs_read_file(const char* name, char* buffer, size_t max_len) {
    int idx = fs_find_node(name);
    if (idx == -1) return -1;
    return fs_read_file_by_idx(idx, buffer, max_len);
}
int fs_read_file_raw(const char* name, void* buffer, size_t offset, size_t max_len) {
    int idx = fs_find_node(name);

    if (idx == -1) return -1;
    return fs_read_file_raw_by_idx(idx, buffer, offset, max_len);
}
int fs_delete_file(const char* name) {
    int idx = fs_find_node(name);
    if (idx < 0) return -1;
    if (idx >= 0 && dir_cache[idx].flags == FS_NODE_DIR && fs_dir_has_children(idx)) return -1;
    if (idx >= 0 && dir_cache[idx].flags == FS_NODE_FILE) {
        uint32_t sectors = node_sector_count(&dir_cache[idx]);
        if (dir_cache[idx].lba != 0 && sectors != 0) fs_zero_sectors(dir_cache[idx].lba, sectors);
    }
    dir_cache[idx].flags = 0;
    dir_cache[idx].size = 0;
    dir_cache[idx].lba = 0;
    dir_cache[idx].parent_index = FS_ROOT_INDEX;
    dir_cache[idx].name[0] = '\0';
    memset(dir_cache[idx].reserved, 0, sizeof(dir_cache[idx].reserved));
    fs_sync();
    return 0;
}
int fs_move_file(const char* name, const char* target_dir) {
    int file_idx = fs_find_node(name);
    if (file_idx == -1) return -1;

    int target_idx = fs_walk_path(target_dir, 0, 0);
    if (target_idx == FS_INVALID_INDEX) return -1;
    if (dir_cache[file_idx].flags == FS_NODE_DIR) {
        int walker = target_idx;
        while (walker >= 0) {
            if (walker == file_idx) return -1;
            walker = dir_cache[walker].parent_index;
        }
    }
    if (fs_find_child(target_idx, dir_cache[file_idx].name, 0) != -1) return -1;

    dir_cache[file_idx].parent_index = target_idx;
    fs_sync();
    return 0;
}
int fs_rename(const char* path, const char* new_name) {
    int idx = fs_find_node(path);
    if (idx == -1 || !fs_is_valid_name(new_name)) return -1;
    if (fs_find_child(dir_cache[idx].parent_index, new_name, 0) != -1) return -1;
    strncpy(dir_cache[idx].name, new_name, 31);
    dir_cache[idx].name[31] = '\0';
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

int fs_list_dir_entries(disk_fs_node_t* out_entries, int max_entries) {
    int count = 0;

    if (!out_entries || max_entries <= 0) return -1;
    for (int i = 0; i < MAX_FILES && count < max_entries; i++) {
        if (dir_cache[i].flags == 0 || dir_cache[i].parent_index != current_dir_index) continue;
        out_entries[count++] = dir_cache[i];
    }
    return count;
}

int fs_get_node_info(int idx, disk_fs_node_t* out_node) {
    if (!out_node) return -1;
    if (idx < 0 || idx >= MAX_FILES) return -1;
    if (dir_cache[idx].flags == 0) return -1;
    *out_node = dir_cache[idx];
    return 0;
}

void get_current_dir_name(char* buf) {
    if (current_dir_index == FS_ROOT_INDEX) {
        buf[0] = '\0';
    } else {
        strncpy(buf, dir_cache[current_dir_index].name, 31);
        buf[31] = '\0';
    }
}

int fs_find_node(const char* path) {
    if (!path || path[0] == '\0') return -1;
    if (strcmp(path, "/") == 0) return FS_ROOT_INDEX;
    if (strcmp(path, ".") == 0) return current_dir_index;
    if (strcmp(path, "..") == 0) return current_dir_index == FS_ROOT_INDEX ? FS_ROOT_INDEX : dir_cache[current_dir_index].parent_index;

    char leaf[32];
    int parent = fs_walk_path(path, 1, leaf);
    if (parent == FS_INVALID_INDEX || leaf[0] == '\0') return -1;
    return fs_find_child(parent, leaf, 0);
}

void fs_get_path_by_index(int idx, char* buf, size_t max_len) {
    char temp[256];
    int segments[32];
    int count = 0;

    if (!buf || max_len == 0) return;
    buf[0] = '\0';
    if (idx == FS_ROOT_INDEX) {
        if (max_len >= 2) {
            buf[0] = '/';
            buf[1] = '\0';
        }
        return;
    }
    if (idx < 0 || idx >= MAX_FILES || dir_cache[idx].flags == 0) return;

    while (idx >= 0 && count < 32) {
        segments[count++] = idx;
        idx = dir_cache[idx].parent_index;
    }

    temp[0] = '\0';
    for (int i = count - 1; i >= 0; i--) {
        size_t len = strlen(temp);
        if (len + 1 >= sizeof(temp)) break;
        temp[len] = '/';
        temp[len + 1] = '\0';
        len++;
        size_t j = 0;
        while (dir_cache[segments[i]].name[j] != '\0' && len + j + 1 < sizeof(temp)) {
            temp[len + j] = dir_cache[segments[i]].name[j];
            j++;
        }
        temp[len + j] = '\0';
    }

    if (temp[0] == '\0') {
        if (max_len >= 2) {
            buf[0] = '/';
            buf[1] = '\0';
        }
        return;
    }

    strncpy(buf, temp, max_len - 1);
    buf[max_len - 1] = '\0';
}

void fs_get_current_path(char* buf, size_t max_len) {
    char temp[256];
    int segments[32];
    int count = 0;

    if (!buf || max_len == 0) return;
    buf[0] = '\0';
    if (current_dir_index == FS_ROOT_INDEX) {
        if (max_len >= 2) {
            buf[0] = '/';
            buf[1] = '\0';
        }
        return;
    }

    int node = current_dir_index;
    while (node >= 0 && count < 32) {
        segments[count++] = node;
        node = dir_cache[node].parent_index;
    }

    temp[0] = '\0';
    for (int i = count - 1; i >= 0; i--) {
        size_t len = strlen(temp);
        if (len + 1 >= sizeof(temp)) break;
        temp[len] = '/';
        temp[len + 1] = '\0';
        len++;
        size_t j = 0;
        while (dir_cache[segments[i]].name[j] != '\0' && len + j + 1 < sizeof(temp)) {
            temp[len + j] = dir_cache[segments[i]].name[j];
            j++;
        }
        temp[len + j] = '\0';
    }

    if (temp[0] == '\0') {
        if (max_len >= 2) {
            buf[0] = '/';
            buf[1] = '\0';
        }
    } else {
        strncpy(buf, temp, max_len - 1);
        buf[max_len - 1] = '\0';
    }
}
