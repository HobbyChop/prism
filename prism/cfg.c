#include "cfg.h"
#include "plat.h"
#include <psp2/io/fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char s_bank[64];
static int s_ahead = 3;
static int s_state = -1;

static void cfg_path(char *out, int n) { snprintf(out, n, "%sprism.cfg", g_app_dir); }

void cfg_load(void)
{
    s_bank[0] = 0;
    char path[300], buf[1024];
    cfg_path(path, sizeof path);
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return;
    int n = sceIoRead(fd, buf, sizeof buf - 1);
    sceIoClose(fd);
    if (n <= 0) return;
    buf[n] = 0;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *cr = strchr(line, '\r');
        if (cr) *cr = 0;
        if (!strncmp(line, "bank=", 5)) snprintf(s_bank, sizeof s_bank, "%.63s", line + 5);
        else if (!strncmp(line, "ahead=", 6)) { s_ahead = atoi(line + 6); if (s_ahead < 1) s_ahead = 1; if (s_ahead > 7) s_ahead = 7; }
        else if (!strncmp(line, "state=", 6)) s_state = atoi(line + 6);
        line = nl ? nl + 1 : NULL;
    }
}

const char *cfg_bank(void) { return s_bank; }

static void cfg_write(void);
int cfg_ahead(void) { return s_ahead; }
int cfg_state(void) { return s_state; }
void cfg_set_state(int idx) { s_state = idx; cfg_write(); }
void cfg_set_ahead(int blocks) { s_ahead = blocks < 1 ? 1 : blocks > 7 ? 7 : blocks; cfg_write(); }

void cfg_set_bank(const char *name)
{
    snprintf(s_bank, sizeof s_bank, "%.63s", name ? name : "");
    cfg_write();
}

static void cfg_write(void)
{
    char path[300], buf[128];
    cfg_path(path, sizeof path);
    int n = snprintf(buf, sizeof buf, "bank=%s\nahead=%d\nstate=%d\n", s_bank, s_ahead, s_state);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return;
    sceIoWrite(fd, buf, n);
    sceIoClose(fd);
}
