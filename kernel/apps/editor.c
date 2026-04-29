#include "editor.h"
#include "fs.h"
#include "string.h"
#define VGA_BASE 0xB8000
#define VGA_COLS 80
#define VGA_ROWS 25
extern void outb(uint16_t port, uint8_t val);
extern volatile uint32_t timer_ticks;
volatile int editor_running = 0;
volatile int editor_input_key = 0;
volatile int editor_special_key = 0;
char editor_buffer[VGA_ROWS - 2][VGA_COLS];
int cursor_col = 0;
int cursor_row = 0;
char current_filename[128];
static void draw_char_direct(int x, int y, char c, uint8_t color) {
    if (x < 0 || x >= VGA_COLS || y < 0 || y >= VGA_ROWS) return;
    volatile uint16_t* vga = (volatile uint16_t*)VGA_BASE;
    vga[y * VGA_COLS + x] = (uint16_t)((unsigned char)c | (color << 8));
}
static void editor_print_string(int x, int y, const char* str, uint8_t color) {
    int i = 0;
    while (str[i] != '\0' && (x + i) < VGA_COLS) {
        draw_char_direct(x + i, y, str[i], color);
        i++;
    }
}
static void editor_clear_row(int y, uint8_t color) {
    for (int x = 0; x < VGA_COLS; x++) {
        draw_char_direct(x, y, ' ', color);
    }
}
static void editor_update_cursor() {
    uint16_t pos = (cursor_row + 1) * VGA_COLS + cursor_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}
static void editor_draw_screen() {
    editor_clear_row(0, 0x1F);
    editor_print_string(2, 0, "NarcVim - ", 0x1F);
    editor_print_string(12, 0, current_filename, 0x1E);
    for (int y = 0; y < VGA_ROWS - 2; y++) {
        for (int x = 0; x < VGA_COLS; x++) {
            draw_char_direct(x, y + 1, editor_buffer[y][x], 0x07);
        }
    }
    editor_clear_row(VGA_ROWS - 1, 0x70);
    editor_print_string(2, VGA_ROWS - 1, "^S Save    ^Q Quit", 0x70);
    editor_update_cursor();
}
static void editor_save_file() {
    char file_data[MAX_TEXT_FILE_SIZE + 1] = {0};
    int data_idx = 0;
    for (int r = 0; r < VGA_ROWS - 2; r++) {
        int last_char = -1;
        for (int c = VGA_COLS - 1; c >= 0; c--) {
            if (editor_buffer[r][c] != ' ' && editor_buffer[r][c] != '\0') {
                last_char = c;
                break;
            }
        }
        for (int c = 0; c <= last_char; c++) {
            if (data_idx < (int)MAX_TEXT_FILE_SIZE - 1) {
                file_data[data_idx++] = editor_buffer[r][c];
            }
        }
        if (last_char >= 0 || r < cursor_row) {
             if (data_idx < (int)MAX_TEXT_FILE_SIZE - 1) {
                 file_data[data_idx++] = '\n';
             }
        }
    }
    file_data[data_idx] = '\0';
    fs_write_file(current_filename, file_data);
    editor_print_string(60, VGA_ROWS - 1, "[ Saved ]", 0x72);
}
static void editor_load_file() {
    char file_data[MAX_TEXT_FILE_SIZE + 1] = {0};
    for (int r = 0; r < VGA_ROWS - 2; r++) {
        for (int c = 0; c < VGA_COLS; c++) {
            editor_buffer[r][c] = ' ';
        }
    }
    if (fs_read_file(current_filename, file_data, sizeof(file_data)) == 0) {
        int r = 0;
        int c = 0;
        for (int i = 0; file_data[i] != '\0'; i++) {
            if (file_data[i] == '\n') {
                r++;
                c = 0;
                if (r >= VGA_ROWS - 2) break;
            } else {
                if (c < VGA_COLS) {
                    editor_buffer[r][c++] = file_data[i];
                }
            }
        }
    }
}
void editor_start(const char* filename) {
    strncpy(current_filename, filename, sizeof(current_filename) - 1);
    current_filename[sizeof(current_filename) - 1] = '\0';
    editor_load_file();
    cursor_col = 0;
    cursor_row = 0;
    editor_running = 1;
    editor_input_key = 0;
    editor_special_key = 0;
    editor_draw_screen();
    while (editor_running) {
        if (editor_input_key != 0) {
            char k = (char)editor_input_key;
            editor_input_key = 0;
            if (k >= 32 && k <= 126) {
                editor_buffer[cursor_row][cursor_col] = k;
                if (cursor_col < VGA_COLS - 1) {
                    cursor_col++;
                }
            }
            editor_draw_screen();
        }
        if (editor_special_key != 0) {
            int sk = editor_special_key;
            editor_special_key = 0;
            if (sk == 1) {
                if (cursor_row > 0) cursor_row--;
            } else if (sk == 2) {
                if (cursor_row < VGA_ROWS - 3) cursor_row++;
            } else if (sk == 3) {
                if (cursor_col > 0) cursor_col--;
                else if (cursor_row > 0) {
                    cursor_row--;
                    cursor_col = VGA_COLS - 1;
                }
            } else if (sk == 4) {
                if (cursor_col < VGA_COLS - 1) cursor_col++;
                else if (cursor_row < VGA_ROWS - 3) {
                    cursor_row++;
                    cursor_col = 0;
                }
            } else if (sk == 5) {
                if (cursor_col > 0) {
                    cursor_col--;
                    editor_buffer[cursor_row][cursor_col] = ' ';
                } else if (cursor_row > 0) {
                    cursor_row--;
                    cursor_col = VGA_COLS - 1;
                    editor_buffer[cursor_row][cursor_col] = ' ';
                }
            } else if (sk == 6) {
                if (cursor_row < VGA_ROWS - 3) {
                    cursor_row++;
                    cursor_col = 0;
                }
            } else if (sk == 7) {
                editor_save_file();
                continue; 
            } else if (sk == 8) {
                editor_running = 0;
                break;
            }
            editor_draw_screen();
        }
        asm volatile("hlt");
    }
    editor_running = 0;
}
