#include <Printf.h>
#include <Syscall.h>
#include <uLib.h>
#include <userfile.h>
#include "../include/file.h"
char* fmtname(char* name) {
    static char buf[STAT_MAX_NAME + 1];
    int len = strlen(name);

    // Return blank-padded name.
    if (len >= STAT_MAX_NAME)
        return name;
    memmove(buf, name, len);
    memset(buf + len, ' ', STAT_MAX_NAME - len);
    buf[STAT_MAX_NAME] = '\0';
    return buf;
}

/*
const char* stat_map[] =
    {[ATTR_DIRECTORY] "T_DIR", [T_FILE] "T_FILE", [T_DEVICE] "D_DEVICE"};
    */


void ls(char *path){
    static char buf[1024];
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd == -1){
        printf("open error\n");
        return;
    }

    for (;;) {
        printf("%s %d\n", __FILE__, __LINE__);
        int nread = getdirent(fd, buf, 1024);
        if (nread == -1)
            printf("getdents error");

        if (nread == 0)
            break;

        // printf("--------------- nread=%d ---------------\n", nread);
        printf("inode#    file type  d_reclen  d_off   d_name\n");
        printf("gerovib  %d\n", nread);
        for (long bpos = 0; bpos < nread;) {
            struct linux_dirent64* d = (struct linux_dirent64*)(buf + bpos);
            printf("  %-9d", d->d_ino);
            u8 d_type = d->d_type;
            printf("%-12s", (d_type == DT_REG)    ? "regular"
                          : (d_type == DT_DIR)  ? "directory"
                          : (d_type == DT_FIFO) ? "FIFO"
                          : (d_type == DT_SOCK) ? "socket"
                          : (d_type == DT_LNK)  ? "symlink"
                          : (d_type == DT_BLK)  ? "block dev"
                          : (d_type == DT_CHR)  ? "char dev"
                                                : "???");
            printf("%-10d%-8d%-8s\n", d->d_reclen, (int)d->d_off, d->d_name);
            bpos += d->d_reclen;
        }
        printf("%s %d\n", __FILE__, __LINE__);
    }
        printf("%s %d\n", __FILE__, __LINE__);
}


int userMain(int argc, char** argv) {
    printf("[ls test]\n");
    printf("argc=%d\n", argc);
    for (int i = 0; i < argc; ++i) {
        printf("%s ", argv[i]);
    }
    printf("\n");
    int i;

    if (argc < 2) {
        // printf("argc <2 branch");
        ls(".");
        return 0;
    }
    for (i = 1; i < argc; i++)
        ls(argv[i]);
    return 0;
}
