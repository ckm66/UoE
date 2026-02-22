#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>    // sleep, getopt
#include <sys/types.h> // pid_t, uid_t
#include <sys/sysinfo.h> // sysconf
#include <pwd.h>      // getpwuid_r
#include <errno.h>
#include <limits.h>   // INT_MAX for strtol safety
#include <time.h>     // nanosleep if needed

// Constants
#define MAX_PATH 256
#define MAX_PIDS 10000
#define MAX_USERS 1000
#define BUFFER_SIZE 4096

// Define DT_DIR if not available (common value 4)
#ifndef DT_DIR
#define DT_DIR 4
#endif
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif

// Global structures for extensibility
typedef struct {
    int pid;   // use int for pid_t portability
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
            long pid_long = strtol(entry->d_name, NULL, 10);
            if (pid_long > 0 && pid_long <= INT_MAX) {
                procs[count].pid = (int)pid_long;
                procs[count].uid = 0;  // placeholder
                procs[count].username = NULL;  // placeholder
                count++;
            }
        }
    }
    closedir(procdir);
    return count;
}

int is_pid_dir(const struct dirent *entry) {
    // Skip . and .. 
    if (entry->d_name[0] == '.')
        return 0;

    // Check d_type if available (DT_DIR == 4, skip if not UNKNOWN)
    unsigned char dtype = entry->d_type;
    if (dtype != DT_UNKNOWN && dtype != DT_DIR)
        return 0;

    // Check all digits and reasonable length
    int len = strlen(entry->d_name);
    if (len < 1 || len > 10) return 0;  // PIDs won't exceed 10 digits soon
    for (int i = 0; i < len; i++) {
        if (!isdigit((unsigned char)entry->d_name[i]))
            return 0;
    }
    return 1;
}

void print_pids(void) {
    for (int i = 0; i < num_processes && i < 20; i++) {
        printf("PID: %d\n", processes[i].pid);
    }
}
