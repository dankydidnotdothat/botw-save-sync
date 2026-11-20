#include <dirent.h>
#include <errno.h>
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

#define MAX_USERS 8
#define COPY_BUF_SIZE (64 * 1024)

typedef struct {
    AccountUid uid;
    char nickname[0x21];
} UserEntry;

static UserEntry g_users[MAX_USERS];
static s32 g_user_count = 0;
static s32 g_selected_user = 0;

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
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int file_exists(const char *path) {
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
    if (!out) {
        fclose(in);
        return -1;
    }

    unsigned char *buf = (unsigned char *)malloc(COPY_BUF_SIZE);
    if (!buf) {
        fclose(in);
        fclose(out);
        return -1;
    }

    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, COPY_BUF_SIZE, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            rc = -1;
            break;
        }
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
            if (copy_dir_recursive(src_path, dst_path, file_count) != 0) {
                closedir(dir);
                return -1;
            }
        } else {
            if (copy_file(src_path, dst_path) != 0) {
                closedir(dir);
                return -1;
            }
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
    if (R_FAILED(rc) || total <= 0) {
        g_user_count = 0;
        return;
    }

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
    printf("\n[>] Mounting BOTW save data...\n");
    consoleUpdate(NULL);

    Result rc = fsdevMountSaveData("save", BOTW_TITLE_ID, uid);
    if (R_FAILED(rc)) {
        printf("[!] Failed to mount BOTW save (0x%x).\n", rc);
        printf("    Make sure BOTW has been launched at least once for this user.\n");
        return;
    }

    mkdir_p(SYNC_DIR);
    remove_dir_contents(SYNC_DIR);

    int count = 0;
    printf("[>] Exporting save:/ -> %s ...\n", SYNC_DIR);
    consoleUpdate(NULL);

    int err = copy_dir_recursive("save:", SYNC_DIR, &count);
    fsdevUnmountDevice("save");

    if (err == 0) {
        printf("[OK] Exported %d files to Sync folder!\n", count);
        printf("     Now click 'Switch -> PC' or 'Auto Sync' in botw_save_sync.py.\n");
    } else {
        printf("[!] Error during export.\n");
    }
}

static void do_restore_from_dir(AccountUid uid, const char *src_dir, int make_backup) {
    char opt_check[512];
    snprintf(opt_check, sizeof(opt_check), "%s/option.sav", src_dir);
    if (!file_exists(opt_check)) {
        printf("\n[!] option.sav not found in:\n    %s\n", src_dir);
        return;
    }

    printf("\n[>] Mounting BOTW save data...\n");
    consoleUpdate(NULL);

    Result rc = fsdevMountSaveData("save", BOTW_TITLE_ID, uid);
    if (R_FAILED(rc)) {
        printf("[!] Failed to mount BOTW save (0x%x).\n", rc);
        return;
    }

    if (make_backup) {
        printf("[>] Creating safety backup -> %s ...\n", BACKUP_DIR);
        consoleUpdate(NULL);
        mkdir_p(BACKUP_DIR);
        remove_dir_contents(BACKUP_DIR);
        int bkp_count = 0;
        copy_dir_recursive("save:", BACKUP_DIR, &bkp_count);
        printf("    Backed up %d files.\n", bkp_count);
    }

    printf("[>] Clearing current BOTW save...\n");
    consoleUpdate(NULL);
    remove_dir_contents("save:");

    int count = 0;
    printf("[>] Importing %s -> save:/ ...\n", src_dir);
    consoleUpdate(NULL);

    int err = copy_dir_recursive(src_dir, "save:", &count);
    if (err == 0) {
        rc = fsdevCommitDevice("save");
        if (R_SUCCEEDED(rc)) {
            printf("[OK] Restored and committed %d files! You can launch BOTW now.\n", count);
        } else {
            printf("[!] fsdevCommitDevice failed (0x%x).\n", rc);
        }
    } else {
        printf("[!] Error copying files into save:/. Changes were NOT committed.\n");
    }

    fsdevUnmountDevice("save");
}

static void draw_header(void) {
    consoleClear();
    printf("============================================================\n");
    printf("   BOTW Save Sync Helper (Switch <-> PC Cemu)  v1.0.0\n");
    printf("============================================================\n");
    printf("Sync folder: %s\n\n", SYNC_DIR);

    if (g_user_count == 0) {
        printf("[!] No Switch user profiles found.\n");
    } else {
        printf("Select Switch profile (Up / Down):\n");
        for (s32 i = 0; i < g_user_count; i++) {
            printf("  %s %s\n", (i == g_selected_user) ? ">>" : "  ", g_users[i].nickname);
        }
    }

    printf("\nControls:\n");
    printf("  [A] Export BOTW save -> SD Sync folder (before syncing to PC)\n");
    printf("  [Y] Import SD Sync folder -> BOTW save (after syncing from PC)\n");
    printf("  [X] Rollback BOTW save from last safety backup\n");
    printf("  [+] Exit\n");
    printf("------------------------------------------------------------\n");
}

int main(int argc, char *argv[]) {
    consoleInit(NULL);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    Result rc = accountInitialize(AccountServiceType_Application);
    if (R_SUCCEEDED(rc)) {
        load_users();
    }

    draw_header();
    printf("Ready. Press [A] to export or [Y] to import.\n");
    consoleUpdate(NULL);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus) break;

        if (g_user_count > 0) {
            if (kDown & HidNpadButton_AnyUp) {
                g_selected_user = (g_selected_user - 1 + g_user_count) % g_user_count;
                draw_header();
                consoleUpdate(NULL);
            } else if (kDown & HidNpadButton_AnyDown) {
                g_selected_user = (g_selected_user + 1) % g_user_count;
                draw_header();
                consoleUpdate(NULL);
            } else if (kDown & HidNpadButton_A) {
                draw_header();
                do_dump_to_sync(g_users[g_selected_user].uid);
                consoleUpdate(NULL);
            } else if (kDown & HidNpadButton_Y) {
                draw_header();
                do_restore_from_dir(g_users[g_selected_user].uid, SYNC_DIR, 1);
                consoleUpdate(NULL);
            } else if (kDown & HidNpadButton_X) {
                draw_header();
                do_restore_from_dir(g_users[g_selected_user].uid, BACKUP_DIR, 0);
                consoleUpdate(NULL);
            }
        }

        consoleUpdate(NULL);
    }

    accountExit();
    consoleExit(NULL);
    return 0;
}
