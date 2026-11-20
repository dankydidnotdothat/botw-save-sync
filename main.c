#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

// Title ID: The Legend of Zelda: Breath of the Wild (Nintendo Switch)
#define BOTW_TITLE_ID 0x01007EF00011E000ULL

// Папка синхронизации (совместима с botw_save_sync.py и JKSV)
#define SYNC_DIR   "sdmc:/JKSV/The Legend of Zelda Breath of the Wild/Sync"
#define BACKUP_DIR "sdmc:/switch/botw_save_sync/backup_last"

#define FB_W 1280
#define FB_H 720

#define MAX_USERS 8
#define MAX_LOGS  5
#define COPY_BUF_SIZE (64 * 1024)

// Палитра (Sheikah Slate / Hyrule Gold)
#define COL_BG          RGBA8_MAXALPHA(14, 20, 31)
#define COL_HEADER      RGBA8_MAXALPHA(20, 29, 45)
#define COL_CARD        RGBA8_MAXALPHA(26, 37, 56)
#define COL_CARD_HOVER  RGBA8_MAXALPHA(34, 49, 74)
#define COL_BORDER      RGBA8_MAXALPHA(52, 72, 102)
#define COL_GOLD        RGBA8_MAXALPHA(212, 175, 55)
#define COL_CYAN        RGBA8_MAXALPHA(45, 212, 191)
#define COL_CORAL       RGBA8_MAXALPHA(245, 158, 11)
#define COL_GREEN       RGBA8_MAXALPHA(34, 197, 94)
#define COL_RED         RGBA8_MAXALPHA(239, 68, 68)
#define COL_TEXT        RGBA8_MAXALPHA(238, 242, 248)
#define COL_MUTED       RGBA8_MAXALPHA(148, 163, 184)
#define COL_BADGE_BG    RGBA8_MAXALPHA(15, 23, 36)

// Компактный растровый шрифт 8x8 (ASCII 32..126)
static const uint8_t FONT8X8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // '!'
    {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00}, // '"'
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, // '#'
    {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00}, // '$'
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00}, // '%'
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, // '&'
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // '\''
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // '('
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // '*'
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ','
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // '.'
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, // '/'
    {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00}, // '0'
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // '1'
    {0x7C,0xC6,0x06,0x1C,0x30,0x60,0xFE,0x00}, // '2'
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, // '3'
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00}, // '4'
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}, // '5'
    {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00}, // '6'
    {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00}, // '7'
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, // '8'
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}, // '9'
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, // ':'
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, // ';'
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, // '<'
    {0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00}, // '='
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, // '>'
    {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00}, // '?'
    {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00}, // '@'
    {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 'A'
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00}, // 'B'
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, // 'C'
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00}, // 'D'
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00}, // 'E'
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00}, // 'F'
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00}, // 'G'
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 'H'
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'I'
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00}, // 'J'
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00}, // 'K'
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00}, // 'L'
    {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00}, // 'M'
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, // 'N'
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 'O'
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, // 'P'
    {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06}, // 'Q'
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00}, // 'R'
    {0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00}, // 'S'
    {0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00}, // 'T'
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 'U'
    {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 'V'
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}, // 'W'
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}, // 'X'
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00}, // 'Y'
    {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00}, // 'Z'
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // '['
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, // '\\'
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // ']'
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // '_'
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00}, // 'a'
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00}, // 'b'
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00}, // 'c'
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00}, // 'd'
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00}, // 'e'
    {0x3C,0x66,0x60,0xF8,0x60,0x60,0xF0,0x00}, // 'f'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x78}, // 'g'
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00}, // 'h'
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // 'i'
    {0x06,0x00,0x0E,0x06,0x06,0x66,0x66,0x3C}, // 'j'
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00}, // 'k'
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'l'
    {0x00,0x00,0xCC,0xFE,0xFE,0xD6,0xC6,0x00}, // 'm'
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00}, // 'n'
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00}, // 'o'
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0}, // 'p'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E}, // 'q'
    {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00}, // 'r'
    {0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00}, // 's'
    {0x30,0x30,0xFC,0x30,0x30,0x36,0x1C,0x00}, // 't'
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00}, // 'u'
    {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 'v'
    {0x00,0x00,0xC6,0xD6,0xFE,0xFE,0x6C,0x00}, // 'w'
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, // 'x'
    {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0x7C}, // 'y'
    {0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00}, // 'z'
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, // '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // '|'
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, // '}'
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}, // '~'
};

typedef struct {
    AccountUid uid;
    char nickname[0x21];
} UserEntry;

typedef struct {
    int x, y, w, h;
} Rect;

static Framebuffer g_fb;
static UserEntry g_users[MAX_USERS];
static s32 g_user_count = 0;
static s32 g_selected_user = 0;

static char g_status_title[96] = "Ready to sync";
static u32  g_status_color = COL_CYAN;
static char g_logs[MAX_LOGS][120];
static int  g_log_count = 0;

// Кнопки действий (координаты для отрисовки и тачскрина)
static const Rect BTN_EXPORT   = { 430, 128, 800, 114 };
static const Rect BTN_IMPORT   = { 430, 260, 800, 114 };
static const Rect BTN_ROLLBACK = { 430, 392, 800, 100 };

static bool pt_in_rect(int px, int py, Rect r) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

static void add_log(const char *fmt, ...) {
    char buf[120];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (g_log_count < MAX_LOGS) {
        strcpy(g_logs[g_log_count++], buf);
    } else {
        for (int i = 1; i < MAX_LOGS; i++) strcpy(g_logs[i - 1], g_logs[i]);
        strcpy(g_logs[MAX_LOGS - 1], buf);
    }
}

static void set_status(const char *title, u32 color) {
    strncpy(g_status_title, title, sizeof(g_status_title) - 1);
    g_status_title[sizeof(g_status_title) - 1] = '\0';
    g_status_color = color;
}

// --- Примитивы отрисовки ---
static void fill_rect(u32 *fb, u32 stride_px, int x, int y, int w, int h, u32 color) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > FB_W ? FB_W : x + w;
    int y1 = y + h > FB_H ? FB_H : y + h;
    for (int py = y0; py < y1; py++) {
        u32 *row = fb + py * stride_px;
        for (int px = x0; px < x1; px++) row[px] = color;
    }
}

static void draw_rect_border(u32 *fb, u32 stride_px, int x, int y, int w, int h, int t, u32 color) {
    fill_rect(fb, stride_px, x, y, w, t, color);
    fill_rect(fb, stride_px, x, y + h - t, w, t, color);
    fill_rect(fb, stride_px, x, y, t, h, color);
    fill_rect(fb, stride_px, x + w - t, y, t, h, color);
}

static void draw_char(u32 *fb, u32 stride_px, int x, int y, char c, int scale, u32 color) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = FONT8X8[c - 32];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                fill_rect(fb, stride_px, x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(u32 *fb, u32 stride_px, int x, int y, const char *text, int scale, u32 color) {
    int cx = x;
    for (const char *p = text; *p; p++) {
        if (cx + 8 * scale > FB_W) break;
        draw_char(fb, stride_px, cx, y, *p, scale, color);
        cx += 8 * scale;
    }
}

static void draw_action_card(
    u32 *fb, u32 stride_px, Rect r, const char *badge, u32 accent,
    const char *title, const char *subtitle
) {
    fill_rect(fb, stride_px, r.x, r.y, r.w, r.h, COL_CARD);
    draw_rect_border(fb, stride_px, r.x, r.y, r.w, r.h, 2, COL_BORDER);
    // Левая акцентная полоса
    fill_rect(fb, stride_px, r.x, r.y, 8, r.h, accent);

    // Значок кнопки [A] / [Y] / [X]
    int bx = r.x + 28;
    int by = r.y + (r.h - 54) / 2;
    fill_rect(fb, stride_px, bx, by, 54, 54, COL_BADGE_BG);
    draw_rect_border(fb, stride_px, bx, by, 54, 54, 2, accent);
    draw_text(fb, stride_px, bx + 15, by + 15, badge, 3, accent);

    // Заголовок и подзаголовок
    draw_text(fb, stride_px, r.x + 104, r.y + 24, title, 3, COL_TEXT);
    draw_text(fb, stride_px, r.x + 104, r.y + 62, subtitle, 2, COL_MUTED);
}

static void render_ui(void) {
    u32 stride = 0;
    u32 *fb = (u32 *)framebufferBegin(&g_fb, &stride);
    if (!fb) return;
    u32 stride_px = stride / sizeof(u32);

    // Фон
    fill_rect(fb, stride_px, 0, 0, FB_W, FB_H, COL_BG);

    // Верхняя шапка
    fill_rect(fb, stride_px, 0, 0, FB_W, 88, COL_HEADER);
    fill_rect(fb, stride_px, 0, 88, FB_W, 3, COL_GOLD);
    draw_text(fb, stride_px, 44, 22, "BOTW SAVE SYNC", 3, COL_GOLD);
    draw_text(fb, stride_px, 44, 54, "Switch <-> PC Cemu Helper", 2, COL_MUTED);
    draw_text(fb, stride_px, 1080, 32, "v2.0 GUI", 2, COL_CYAN);

    // Левая панель: Профили Switch
    fill_rect(fb, stride_px, 44, 128, 356, 364, COL_CARD);
    draw_rect_border(fb, stride_px, 44, 128, 356, 364, 2, COL_BORDER);
    draw_text(fb, stride_px, 64, 146, "SWITCH PROFILES", 2, COL_GOLD);
    fill_rect(fb, stride_px, 64, 174, 316, 2, COL_BORDER);

    if (g_user_count == 0) {
        draw_text(fb, stride_px, 64, 200, "No profiles found", 2, COL_RED);
    } else {
        for (s32 i = 0; i < g_user_count && i < 5; i++) {
            int py = 190 + i * 56;
            bool sel = (i == g_selected_user);
            fill_rect(fb, stride_px, 60, py, 324, 46, sel ? COL_CARD_HOVER : COL_BADGE_BG);
            draw_rect_border(fb, stride_px, 60, py, 324, 46, 2, sel ? COL_GOLD : COL_BORDER);
            if (sel) fill_rect(fb, stride_px, 60, py, 6, 46, COL_GOLD);
            draw_text(fb, stride_px, 78, py + 15, g_users[i].nickname, 2, sel ? COL_TEXT : COL_MUTED);
        }
    }

    // Правая панель: 3 кнопки действий
    draw_action_card(
        fb, stride_px, BTN_EXPORT, "A", COL_CYAN,
        "EXPORT TO PC (SD SYNC)",
        "Copy Switch BOTW save -> SD Sync folder"
    );
    draw_action_card(
        fb, stride_px, BTN_IMPORT, "Y", COL_GOLD,
        "IMPORT FROM PC (SD SYNC)",
        "Auto-backup + Restore SD Sync -> Switch"
    );
    draw_action_card(
        fb, stride_px, BTN_ROLLBACK, "X", COL_CORAL,
        "ROLLBACK LAST BACKUP",
        "Restore previous safety backup to Switch"
    );

    // Нижняя панель: Статус и журнал
    fill_rect(fb, stride_px, 44, 514, 1186, 152, COL_CARD);
    draw_rect_border(fb, stride_px, 44, 514, 1186, 152, 2, COL_BORDER);
    fill_rect(fb, stride_px, 44, 514, 1186, 4, g_status_color);

    draw_text(fb, stride_px, 64, 530, g_status_title, 2, g_status_color);
    for (int i = 0; i < g_log_count; i++) {
        draw_text(fb, stride_px, 64, 560 + i * 20, g_logs[i], 2, COL_MUTED);
    }

    // Футер с подсказками
    fill_rect(fb, stride_px, 0, 680, FB_W, 40, COL_HEADER);
    draw_text(
        fb, stride_px, 44, 692,
        "[Up/Down] Profile   [A] Export   [Y] Import   [X] Rollback   [Touch] Tap   [+] Exit",
        2, COL_MUTED
    );

    framebufferEnd(&g_fb);
}

// --- Файловые операции ---
static int mkdir_p(const char *path) {
    char tmp[512];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    strcpy(tmp, path);
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
    char *p = strchr(tmp, ':');
    p = p ? p + 1 : tmp;
    if (*p == '/') p++;
    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
    return 0;
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int remove_dir_contents(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;
    struct dirent *ent;
    char child[512];
    int rc = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        snprintf(child, sizeof(child), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (remove_dir_contents(child) != 0) rc = -1;
            if (rmdir(child) != 0) rc = -1;
        } else {
            if (unlink(child) != 0) rc = -1;
        }
    }
    closedir(dir);
    return rc;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    unsigned char *buf = (unsigned char *)malloc(COPY_BUF_SIZE);
    if (!buf) { fclose(in); fclose(out); return -1; }
    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, COPY_BUF_SIZE, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    }
    if (ferror(in)) rc = -1;
    free(buf);
    fclose(in);
    fclose(out);
    return rc;
}

static int copy_dir_recursive(const char *src_dir, const char *dst_dir, int *file_count) {
    if (mkdir_p(dst_dir) != 0) return -1;
    DIR *dir = opendir(src_dir);
    if (!dir) return -1;
    struct dirent *ent;
    char src_path[512];
    char dst_path[512];
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, ent->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, ent->d_name);
        struct stat st;
        if (stat(src_path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (copy_dir_recursive(src_path, dst_path, file_count) != 0) { closedir(dir); return -1; }
        } else {
            if (copy_file(src_path, dst_path) != 0) { closedir(dir); return -1; }
            if (file_count) (*file_count)++;
        }
    }
    closedir(dir);
    return 0;
}

static void load_users(void) {
    AccountUid uids[MAX_USERS];
    s32 total = 0;
    Result rc = accountListAllUsers(uids, MAX_USERS, &total);
    if (R_FAILED(rc) || total <= 0) { g_user_count = 0; return; }
    g_user_count = 0;
    for (s32 i = 0; i < total && i < MAX_USERS; i++) {
        g_users[g_user_count].uid = uids[i];
        snprintf(g_users[g_user_count].nickname, sizeof(g_users[g_user_count].nickname), "User %d", i + 1);
        AccountProfile profile;
        if (R_SUCCEEDED(accountGetProfile(&profile, uids[i]))) {
            AccountProfileBase base;
            if (R_SUCCEEDED(accountProfileGet(&profile, NULL, &base))) {
                strncpy(g_users[g_user_count].nickname, base.nickname, sizeof(g_users[g_user_count].nickname) - 1);
                g_users[g_user_count].nickname[sizeof(g_users[g_user_count].nickname) - 1] = '\0';
            }
            accountProfileClose(&profile);
        }
        g_user_count++;
    }
}

static void do_dump_to_sync(AccountUid uid) {
    set_status("Exporting BOTW save to SD Sync...", COL_CYAN);
    add_log("> Mounting BOTW save data...");
    render_ui();

    Result rc = fsdevMountSaveData("save", BOTW_TITLE_ID, uid);
    if (R_FAILED(rc)) {
        set_status("Export failed: could not mount BOTW save", COL_RED);
        add_log("! Mount failed (0x%x). Launch BOTW once first.", rc);
        return;
    }

    mkdir_p(SYNC_DIR);
    remove_dir_contents(SYNC_DIR);

    int count = 0;
    int err = copy_dir_recursive("save:", SYNC_DIR, &count);
    fsdevUnmountDevice("save");

    if (err == 0) {
        set_status("Export complete! Ready for PC sync.", COL_GREEN);
        add_log("OK: Exported %d files to SD Sync folder.", count);
    } else {
        set_status("Export failed during file copy.", COL_RED);
        add_log("! File copy error during export.");
    }
}

static void do_restore_from_dir(AccountUid uid, const char *src_dir, bool make_backup) {
    char opt_check[512];
    snprintf(opt_check, sizeof(opt_check), "%s/option.sav", src_dir);
    if (!file_exists(opt_check)) {
        set_status("Import failed: option.sav not found", COL_RED);
        add_log("! Missing option.sav in %s", src_dir);
        return;
    }

    set_status(make_backup ? "Importing SD Sync -> BOTW..." : "Rolling back backup -> BOTW...", COL_GOLD);
    add_log("> Mounting BOTW save data...");
    render_ui();

    Result rc = fsdevMountSaveData("save", BOTW_TITLE_ID, uid);
    if (R_FAILED(rc)) {
        set_status("Import failed: could not mount BOTW save", COL_RED);
        add_log("! Mount failed (0x%x).", rc);
        return;
    }

    if (make_backup) {
        add_log("> Creating safety backup...");
        render_ui();
        mkdir_p(BACKUP_DIR);
        remove_dir_contents(BACKUP_DIR);
        int bkp = 0;
        copy_dir_recursive("save:", BACKUP_DIR, &bkp);
        add_log("  Safety backup saved (%d files).", bkp);
    }

    remove_dir_contents("save:");
    int count = 0;
    int err = copy_dir_recursive(src_dir, "save:", &count);
    if (err == 0) {
        rc = fsdevCommitDevice("save");
        if (R_SUCCEEDED(rc)) {
            set_status("Import complete! You can launch BOTW now.", COL_GREEN);
            add_log("OK: Restored and committed %d files.", count);
        } else {
            set_status("Commit error! Save not updated.", COL_RED);
            add_log("! fsdevCommitDevice failed (0x%x).", rc);
        }
    } else {
        set_status("Copy error! Changes were NOT committed.", COL_RED);
        add_log("! Copy error; save left untouched.");
    }

    fsdevUnmountDevice("save");
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    NWindow *win = nwindowGetDefault();
    framebufferCreate(&g_fb, win, FB_W, FB_H, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&g_fb);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();

    if (R_SUCCEEDED(accountInitialize(AccountServiceType_Application))) {
        load_users();
    }

    add_log("Sync folder: %s", SYNC_DIR);
    add_log("Select profile and press [A] Export or [Y] Import.");

    bool prev_touch = false;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;

        // Обработка тачскрина (нажатие по отпусканию / первому касанию)
        HidTouchScreenState touch = {0};
        bool touch_click = false;
        int tx = 0, ty = 0;
        if (hidGetTouchScreenStates(&touch, 1) && touch.count > 0) {
            if (!prev_touch) {
                touch_click = true;
                tx = (int)touch.touches[0].x;
                ty = (int)touch.touches[0].y;
            }
            prev_touch = true;
        } else {
            prev_touch = false;
        }

        if (g_user_count > 0) {
            if (kDown & HidNpadButton_AnyUp) {
                g_selected_user = (g_selected_user - 1 + g_user_count) % g_user_count;
            } else if (kDown & HidNpadButton_AnyDown) {
                g_selected_user = (g_selected_user + 1) % g_user_count;
            } else if ((kDown & HidNpadButton_A) || (touch_click && pt_in_rect(tx, ty, BTN_EXPORT))) {
                do_dump_to_sync(g_users[g_selected_user].uid);
            } else if ((kDown & HidNpadButton_Y) || (touch_click && pt_in_rect(tx, ty, BTN_IMPORT))) {
                do_restore_from_dir(g_users[g_selected_user].uid, SYNC_DIR, true);
            } else if ((kDown & HidNpadButton_X) || (touch_click && pt_in_rect(tx, ty, BTN_ROLLBACK))) {
                do_restore_from_dir(g_users[g_selected_user].uid, BACKUP_DIR, false);
            } else if (touch_click) {
                // Проверка нажатия на карточку профиля
                for (s32 i = 0; i < g_user_count && i < 5; i++) {
                    Rect pr = { 60, 190 + i * 56, 324, 46 };
                    if (pt_in_rect(tx, ty, pr)) {
                        g_selected_user = i;
                        break;
                    }
                }
            }
        }

        render_ui();
    }

    accountExit();
    framebufferClose(&g_fb);
    return 0;
}