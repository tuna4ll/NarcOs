// kernel/snake.c
#include <stdint.h>

#define VGA_BASE  0xB8000
#define VGA_COLS  80
#define VGA_ROWS  25

#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3


extern volatile uint32_t timer_ticks;

int snake_x[2000];
int snake_y[2000];
int snake_len;
int snake_dir;
int apple_x;
int apple_y;
volatile int is_dead;
volatile int snake_running = 0;
volatile int snake_next_dir;
int score    = 0;
int level    = 1;
int apples   = 0; // Toplam yenilen elma sayısı



void draw_char_direct(int x, int y, char c, uint8_t color)
{
    if (x < 0 || x >= VGA_COLS || y < 0 || y >= VGA_ROWS) return;
    volatile uint16_t* vga = (volatile uint16_t*)VGA_BASE;
    vga[y * VGA_COLS + x] = (uint16_t)((unsigned char)c | (color << 8));
}

void vga_print_direct(int x, int y, const char* str, uint8_t color)
{
    int i = 0;
    while (str[i] != '\0') {
        if (x + i >= VGA_COLS) break;
        draw_char_direct(x + i, y, str[i], color);
        i++;
    }
}

void vga_print_int_direct(int x, int y, int num, uint8_t color)
{
    char buf[16];
    int pos = 0;

    if (num == 0) {
        buf[pos++] = '0';
    } else {
        int n = num;
        while (n > 0) { buf[pos++] = (char)((n % 10) + '0'); n /= 10; }
    }

    int px = x;
    for (int i = pos - 1; i >= 0; i--) {
        if (px >= VGA_COLS) break;
        draw_char_direct(px, y, buf[i], color);
        px++;
    }
    // Eski haneyi temizle
    if (px < VGA_COLS) draw_char_direct(px, y, ' ', color);
}

void fill_row(int y, char c, uint8_t color)
{
    for (int x = 0; x < VGA_COLS; x++)
        draw_char_direct(x, y, c, color);
}

void fill_rect(int x, int y, int w, int h, char c, uint8_t color)
{
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            draw_char_direct(x + dx, y + dy, c, color);
}



void draw_ui()
{
    fill_row(0, ' ', 0x17);
    draw_char_direct(1, 0, 0xC9, 0x1E); // ╔  (sarı-mavi)
    draw_char_direct(2, 0, 0xCD, 0x1E);
    draw_char_direct(3, 0, 0xCD, 0x1E);
    draw_char_direct(4, 0, 0xBB, 0x1E); // ╗
    vga_print_direct(6, 0, "NarcOS",     0x1F); // Parlak beyaz
    vga_print_direct(13, 0, "SNAKE",     0x1E); // Sarı

    // Orta: dekorasyon
    vga_print_direct(22, 0, (const char[]){0xC4,0xC4,0xB4,' ','v','1','.','0',' ',0xC3,0xC4,0xC4,0}, 0x18);

    vga_print_direct(60, 0, "[ Use WASD or Arrows ]", 0x19);

    fill_row(1, ' ', 0x07);

    // SKOR kutusu
    draw_char_direct(1, 1, 0xDA, 0x0A);  // ┌ Yeşil
    draw_char_direct(2, 1, 0xC4, 0x0A);
    draw_char_direct(3, 1, 0xC4, 0x0A);
    draw_char_direct(4, 1, 0xC4, 0x0A);
    draw_char_direct(5, 1, 0xC4, 0x0A);
    draw_char_direct(6, 1, 0xC4, 0x0A);
    draw_char_direct(7, 1, 0xC4, 0x0A);
    draw_char_direct(8, 1, 0xC4, 0x0A);
    draw_char_direct(9, 1, 0xC4, 0x0A);
    draw_char_direct(10, 1, 0xBF, 0x0A); // ┐

    // LEVEL kutusu
    draw_char_direct(22, 1, 0xDA, 0x0E); // ┌ Sarı
    draw_char_direct(32, 1, 0xBF, 0x0E); // ┐
    for (int x = 23; x < 32; x++) draw_char_direct(x, 1, 0xC4, 0x0E);

    // HIZ kutusu
    draw_char_direct(44, 1, 0xDA, 0x0B); // ┌ Cyan
    draw_char_direct(58, 1, 0xBF, 0x0B); // ┐
    for (int x = 45; x < 58; x++) draw_char_direct(x, 1, 0xC4, 0x0B);

    // ELMA sayacı
    draw_char_direct(65, 1, 0xDA, 0x0C); // ┌ Kırmızı
    draw_char_direct(78, 1, 0xBF, 0x0C); // ┐
    for (int x = 66; x < 78; x++) draw_char_direct(x, 1, 0xC4, 0x0C);
    draw_char_direct(0,  2, 0xC9, 0x08);
    draw_char_direct(79, 2, 0xBB, 0x08);
    for (int x = 1; x < 79; x++) draw_char_direct(x, 2, 0xCD, 0x08);
    for (int y = 3; y < 24; y++) {
        draw_char_direct(0,  y, 0xBA, 0x08);
        draw_char_direct(79, y, 0xBA, 0x08);
        for (int x = 1; x < 79; x++) draw_char_direct(x, y, ' ', 0x0F);
    }
    draw_char_direct(0,  24, 0xC8, 0x08);
    draw_char_direct(79, 24, 0xBC, 0x08);
    for (int x = 1; x < 79; x++) draw_char_direct(x, 24, 0xCD, 0x08);
    vga_print_direct(2,  24, " R: Restart  Q/ESC: Quit ", 0x08);
}

void update_status_bar()
{
    // SCORE
    vga_print_direct(2,  1, "SCORE:",  0x0A);
    vga_print_int_direct(8, 1, score, 0x0F);

    // LEVEL
    vga_print_direct(24, 1, "LEVEL:", 0x0E);
    vga_print_int_direct(31, 1, level, 0x0F);

    // SPEED (ms per tick)
    vga_print_direct(46, 1, "SPEED:", 0x0B);
    // Daha yüksek level = daha az tick = daha hızlı
    int speed_display = 15 - (level - 1) * 2;
    if (speed_display < 5) speed_display = 5;
    vga_print_int_direct(51, 1, speed_display * 10, 0x0F);
    vga_print_direct(55, 1, " ms", 0x0B);

    // ELMA
    draw_char_direct(67, 1, 0xFE, 0x0C); // ■ kırmızı elma ikonu
    vga_print_direct(69, 1, "x", 0x07);
    vga_print_int_direct(71, 1, apples, 0x0F);
}



void spawn_apple()
{
    // Oyun alanı: x=1..78, y=3..23
    apple_x = (int)(timer_ticks % 77) + 1;
    apple_y = (int)(timer_ticks % 20) + 3;

    for (int i = 0; i < snake_len; i++) {
        if (snake_x[i] == apple_x && snake_y[i] == apple_y) {
            apple_x = (int)((timer_ticks + i * 13 + 7) % 77) + 1;
            apple_y = (int)((timer_ticks + i * 7  + 3) % 20) + 3;
        }
    }
}



void update_snake()
{
    // Geçerli yön değişikliği kontrolü
    if (!((snake_dir == DIR_UP    && snake_next_dir == DIR_DOWN)  ||
          (snake_dir == DIR_DOWN  && snake_next_dir == DIR_UP)    ||
          (snake_dir == DIR_LEFT  && snake_next_dir == DIR_RIGHT) ||
          (snake_dir == DIR_RIGHT && snake_next_dir == DIR_LEFT)))
    {
        snake_dir = snake_next_dir;
    }

    // Kuyruğu Sil
    draw_char_direct(snake_x[snake_len - 1], snake_y[snake_len - 1], ' ', 0x0F);

    // Vücudu Kaydır
    for (int i = snake_len - 1; i > 0; i--) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }

    // Başı Hareket Ettir
    if (snake_dir == DIR_UP)    snake_y[0]--;
    if (snake_dir == DIR_DOWN)  snake_y[0]++;
    if (snake_dir == DIR_LEFT)  snake_x[0]--;
    if (snake_dir == DIR_RIGHT) snake_x[0]++;

    // Çarpışma: Duvarlar (alan x=1..78, y=3..23)
    if (snake_x[0] <= 0 || snake_x[0] >= VGA_COLS - 1 ||
        snake_y[0] <= 2 || snake_y[0] >= VGA_ROWS - 1)
    {
        is_dead = 1;
        return;
    }

    // Çarpışma: Kendi gövdesi
    for (int i = 1; i < snake_len; i++) {
        if (snake_x[0] == snake_x[i] && snake_y[0] == snake_y[i]) {
            is_dead = 1;
            return;
        }
    }

    // Elma yendi mi?
    if (snake_x[0] == apple_x && snake_y[0] == apple_y) {
        if (snake_len < 2000) snake_len++;
        apples++;
        score += 10 * level;

        // Her 5 elmada bir level atla
        level = (apples / 5) + 1;
        if (level > 9) level = 9;

        update_status_bar();
        spawn_apple();
    }

    // Çizim
    draw_char_direct(apple_x, apple_y, 0xFE, 0x0C); // ■ Kırmızı elma, parlak

    // Yılan gövdesi: gradient yeşil
    for (int i = snake_len - 1; i >= 1; i--) {
        uint8_t col = (i % 2 == 0) ? 0x02 : 0x0A; // koyu/açık yeşil dalgası
        draw_char_direct(snake_x[i], snake_y[i], 0xB2, col); // ▒
    }
    // Baş: Parlak sarı, yön karakteri
    char head_char;
    switch (snake_dir) {
        case DIR_UP:    head_char = 0x1E; break; // ▲
        case DIR_DOWN:  head_char = 0x1F; break; // ▼
        case DIR_LEFT:  head_char = 0x11; break; // ◄
        case DIR_RIGHT: head_char = 0x10; break; // ►
        default:        head_char = 0xDB; break;
    }
    draw_char_direct(snake_x[0], snake_y[0], head_char, 0x0E); // Sarı baş
}



void delay(int ticks)
{
    if (ticks <= 0) return;
    volatile uint32_t target   = timer_ticks + (uint32_t)ticks;
    volatile uint32_t failsafe = 0;
    while (timer_ticks < target) {
        asm volatile("nop");
        if (++failsafe > 50000000UL) break;
    }
}



void draw_game_over()
{
    int bx = 20, by = 8;
    int bw = 40, bh = 9;

    // Arka plan kutusu — koyu kırmızı
    fill_rect(bx, by, bw, bh, ' ', 0x4F);

    // Çerçeve
    draw_char_direct(bx,         by,         0xC9, 0x4E); // ╔
    draw_char_direct(bx + bw-1,  by,         0xBB, 0x4E); // ╗
    draw_char_direct(bx,         by + bh-1,  0xC8, 0x4E); // ╚
    draw_char_direct(bx + bw-1,  by + bh-1,  0xBC, 0x4E); // ╝
    for (int x = bx+1; x < bx+bw-1; x++) {
        draw_char_direct(x, by,        0xCD, 0x4E);
        draw_char_direct(x, by + bh-1, 0xCD, 0x4E);
    }
    for (int y = by+1; y < by+bh-1; y++) {
        draw_char_direct(bx,        y, 0xBA, 0x4E);
        draw_char_direct(bx + bw-1, y, 0xBA, 0x4E);
    }

    vga_print_direct(bx + 12, by + 1, "GAME  OVER",        0x4E);
    for (int x = bx+1; x < bx+bw-1; x++) draw_char_direct(x, by+2, 0xC4, 0x4F);

    vga_print_direct(bx + 5,  by + 3, "Score :", 0x4F);
    vga_print_int_direct(bx + 13, by + 3, score,  0x4E);

    vga_print_direct(bx + 5,  by + 4, "Level :", 0x4F);
    vga_print_int_direct(bx + 13, by + 4, level,  0x4E);

    vga_print_direct(bx + 5,  by + 5, "Apple :", 0x4F);
    vga_print_int_direct(bx + 13, by + 5, apples, 0x4E);

    for (int x = bx+1; x < bx+bw-1; x++) draw_char_direct(x, by+6, 0xC4, 0x4F);

    vga_print_direct(bx + 5,  by + 7, "[ R ] Restart  [ Q ] Quit", 0x4F);
}



void snake_main()
{
    int retry = 1;
    snake_running = 1;

    while (retry) {
        score      = 0;
        level      = 1;
        apples     = 0;
        snake_len  = 5;
        snake_dir  = DIR_RIGHT;
        snake_next_dir = DIR_RIGHT;
        is_dead    = 0;

        draw_ui();
        update_status_bar();

        // Başlangıç pozisyonu (y=13 → alan içi)
        for (int i = 0; i < snake_len; i++) {
            snake_x[i] = 40 - i;
            snake_y[i] = 13;
        }
        spawn_apple();

        /* ── Oyun Döngüsü ── */
        while (!is_dead) {
            update_snake();
            if (is_dead) break;

            /*
             * Hız hesabı:
             *   Level 1 → 15 tick = ~150ms (klasik snake hissi)
             *   Level 2 → 13 tick = ~130ms
             *   ...
             *   Level 9 →  5 tick =  ~50ms (çok hızlı)
             */
            int speed = 15 - (level - 1) * 2;
            if (speed < 5) speed = 5;
            delay(speed);
        }

        /* ── Game Over ── */
        draw_game_over();

        // Önceki yön değerini sıfırla
        snake_next_dir = -1;
        retry = 0;

        int waiting = 1;
        while (waiting) {
            if      (snake_next_dir == 5) { retry = 1; waiting = 0; }
            else if (snake_next_dir == 6) { retry = 0; waiting = 0; }
            delay(5);
        }
    }

    snake_running = 0;
}