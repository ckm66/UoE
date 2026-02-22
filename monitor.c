#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>  // for sleep, getopt
#include <time.h>    // for nanosleep if needed
#include <sys/sysinfo.h>  // sysconf
#include <pwd.h>     // getpwuid_r
#include <errno.h>

// Constants
#define MAX_PATH 256
#define MAX_PIDS 10000  // reasonable limit
#define MAX_USERS 1000
#define BUFFER_SIZE 4096

// Global structures for extensibility
typedef struct {
    pid_t pid;
    uid_t uid;
    char *username;  // will alloc later
} ProcessInfo;

ProcessInfo *processes;
int num_processes = 0;

// Function prototypes
int enumerate_pids(ProcessInfo *procs, int max_procs);
int is_pid_dir(const struct dirent *entry);
void print_pids(void);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <seconds>\n", argv[0]);
        return 1;
    }
    int duration = atoi(argv[1]);
    if (duration <= 0) {
        fprintf(stderr, "Duration must be positive integer\n");
        return 1;
    }

    processes = malloc(MAX_PIDS * sizeof(ProcessInfo));
    if (!processes) {
        perror("malloc");
        return 1;
    }

    // Step 1: Test enumeration
    printf("Testing PID enumeration (first 20 PIDs):\n");
    num_processes = enumerate_pids(processes, MAX_PIDS);
    if (num_processes > 0) {
        print_pids();
        printf("Total PIDs found: %d\n", num_processes);
    } else {
        printf("No PIDs found!\n");
    }

    free(processes);
    return 0;
}

int enumerate_pids(ProcessInfo *procs, int max_procs) {
    DIR *procdir = opendir("/proc");
    if (!procdir) {
        perror("opendir /proc");
        return 0;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(procdir)) != NULL && count < max_procs) {
        if (is_pid_dir(entry)) {
            pid_t pid = atoi(entry->d_name);
            procs[count].pid = pid;
            procs[count].uid = 0;  // placeholder
            procs[count].username = NULL;  // placeholder
            count++;
        }
    }
    closedir(procdir);
    return count;
}

int is_pid_dir(const struct dirent *entry) {
    if (entry->d_type != DT_DIR)
        return 0;
    for (char *p = entry->d_name; *p; p++) {
        if (!isdigit((unsigned char)*p))
            return 0;
    }
    return 1;
}

void print_pids(void) {
    for (int i = 0; i < num_processes && i < 20; i++) {
        printf("PID: %d\n", processes[i].pid);
    }
}
