#include "user_lib.h"

#define C_RESET "\033[0m"
#define C_DIM "\033[90m"
#define C_RED "\033[1;31m"
#define C_GREEN "\033[1;32m"
#define C_YELLOW "\033[1;33m"
#define C_BLUE "\033[1;34m"
#define C_MAGENTA "\033[1;35m"
#define C_CYAN "\033[1;36m"

#define C_NAME C_CYAN
#define C_ROLE C_GREEN
#define C_LOGO_A C_CYAN
#define C_LOGO_B C_BLUE
#define C_LOGO_C C_MAGENTA
#define C_LOGO_D C_RED

#define CREDITS_LOGO_WIDTH 30U

static const char* credits_logo[] = {
    "        @%%%%%%%%%%%%",
    "        %%%%%%%%%%%%%%@",
    "        %%%%%%%%%%%%%%%%",
    "        %%%%%%%%%%%%%%%%%%",
    "         %%%%%%%%%%%%%%%%%",
    "                 %%%%%%%%%",
    "                  %%%%%%%%",
    "    %%%%           %%%%%%%",
    "   %%%%%@          %%%%%%%",
    "  %%%%%%%          %%%%%%%",
    " @%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%%         %%%%%%%",
    "%%%%%%%%%          %%%%%%%"
};

static const char* logo_color_for_line(uint32_t line) {
    if (line < 6U) return C_LOGO_A;
    if (line < 13U) return C_LOGO_B;
    return C_LOGO_D;
}

static int print_text(const char* text) {
    return userlib_print(text);
}

static int print_newline(void) {
    return userlib_write_all(USER_STDOUT, "\n", 1U);
}

static int print_logo_prefix(const char* logo, const char* color) {
    uint32_t len = (uint32_t)userlib_strlen(logo);

    if (color && print_text(color) != 0) return -1;
    if (print_text(logo) != 0) return -1;
    if (print_text(C_RESET) != 0) return -1;
    while (len < CREDITS_LOGO_WIDTH) {
        if (print_text(" ") != 0) return -1;
        len++;
    }
    return print_text("  ");
}

static int print_text_line(uint32_t logo_line, const char* text) {
    if (print_logo_prefix(credits_logo[logo_line], logo_color_for_line(logo_line)) != 0) return -1;
    if (print_text(text) != 0) return -1;
    return print_newline();
}

static int print_logo_only_line(uint32_t logo_line) {
    if (print_logo_prefix(credits_logo[logo_line], logo_color_for_line(logo_line)) != 0) return -1;
    return print_newline();
}

static int print_person_line(uint32_t logo_line, const char* name, const char* role) {
    if (print_logo_prefix(credits_logo[logo_line], logo_color_for_line(logo_line)) != 0) return -1;
    if (print_text(C_NAME) != 0) return -1;
    if (print_text(name) != 0) return -1;
    if (print_text(C_RESET " - " C_ROLE) != 0) return -1;
    if (print_text(role) != 0) return -1;
    if (print_text(C_RESET) != 0) return -1;
    return print_newline();
}

static int print_color_blocks(uint32_t logo_line, int bright) {
    static const char* normal[] = {
        "\033[40m   ", "\033[41m   ", "\033[42m   ", "\033[43m   ",
        "\033[44m   ", "\033[45m   ", "\033[46m   ", "\033[47m   "
    };
    static const char* bright_blocks[] = {
        "\033[100m   ", "\033[101m   ", "\033[102m   ", "\033[103m   ",
        "\033[104m   ", "\033[105m   ", "\033[106m   ", "\033[107m   "
    };
    const char* const* blocks = bright ? bright_blocks : normal;

    if (print_logo_prefix(credits_logo[logo_line], logo_color_for_line(logo_line)) != 0) return -1;
    for (uint32_t i = 0; i < 8U; i++) {
        if (print_text(blocks[i]) != 0) return -1;
    }
    if (print_text(C_RESET) != 0) return -1;
    return print_newline();
}

int main(void) {
    uint32_t logo_count = (uint32_t)(sizeof(credits_logo) / sizeof(credits_logo[0]));
    uint32_t line = 0;

    if (print_text_line(line++, C_CYAN "root" C_RESET "@" C_MAGENTA "narcOs" C_RESET) != 0) return 1;
    if (print_text_line(line++, C_DIM "---------------" C_RESET) != 0) return 1;
    if (print_text_line(line++, "") != 0) return 1;
    if (print_text_line(line++, C_YELLOW "Credits" C_RESET) != 0) return 1;
    if (print_person_line(line++, "Tuna Kilic (Tuna4l)", "Maintainer") != 0) return 1;
    if (print_person_line(line++, "Burak Elibol", "Designer") != 0) return 1;
    if (print_text_line(line++, "") != 0) return 1;
    if (print_text_line(line++, C_DIM "Built with caffeine." C_RESET) != 0) return 1;
    if (print_color_blocks(line++, 0) != 0) return 1;
    if (print_color_blocks(line++, 1) != 0) return 1;

    while (line < logo_count) {
        if (print_logo_only_line(line++) != 0) return 1;
    }
    return 0;
}
