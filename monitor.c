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
#include <limits.h>   // INT_MAX
#include <time.h>

// Constants
#define MAX_PATH 256
#define MAX_PIDS 20000
#define MAX_USERS 1000
#define BUFFER_SIZE 4096

// Define DT_DIR if not available
#ifndef DT_DIR
#define DT_DIR 4
#endif
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif

typedef struct {
    int pid;
    uid_t uid;
    char *username;
    unsigned long utime;     // field 14: user ticks
    unsigned long stime;     // field 15: kernel ticks
    unsigned long starttime; // field 22: start ticks
    double uptime_secs;      // process uptime in seconds
    double cpu_secs;         // total cpu time in seconds (placeholder)
} ProcessInfo;

ProcessInfo *processes;
int num_processes = 0;
long clk_tck;  // global clock ticks per second

// Prototypes
int enumerate_pids(ProcessInfo *procs, int max_procs);
int is_pid_dir(const struct dirent *entry);
int parse_stat(int pid, ProcessInfo *info);
double get_uptime_secs(void);
void print_all_info(void);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <seconds>\n", argv[0]);
        return 1;
    }
    int duration = atoi(argv[1]);
    if (duration <= 0) {
        fprintf(stderr, "Duration must be positive\n");
        return 1;
    }

    clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) {
        perror("sysconf CLK_TCK");
        return 1;
    }
    printf("CLK_TCK: %ld\n", clk_tck);

    processes = malloc(MAX_PIDS * sizeof(ProcessInfo));
    if (!processes) {
        perror("malloc");
        return 1;
    }

    // Step 2: Enumerate + parse stat
    printf("Enumerating and parsing /proc/[pid]/stat...\n");
    num_processes = enumerate_pids(processes, MAX_PIDS);
    int parsed = 0;
    for (int i = 0; i < num_processes; i++) {
        if (parse_stat(processes[i].pid, &processes[i])) {
            double uptime = get_uptime_secs() - processes[i].uptime_secs;
            processes[i].uptime_secs = uptime;
            processes[i].cpu_secs = (processes[i].utime + processes[i].stime) / (double)clk_tck;
            parsed++;
        }
    }
    printf("Parsed %d/%d processes\n", parsed, num_processes);

    print_all_info();
    printf("Total PIDs: %d\n", num_processes);

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
    while ((entry = readdir(procdir)) && count < max_procs) {
        if (is_pid_dir(entry)) {
            long pid_long = strtol(entry->d_name, NULL, 10);
            if (pid_long > 0 && pid_long <= INT_MAX) {
                procs[count].pid = (int)pid_long;
                procs[count].uid = 0;
                procs[count].username = NULL;
                procs[count].utime = procs[count].stime = procs[count].starttime = 0;
                procs[count].uptime_secs = 0;
                procs[count].cpu_secs = 0;
                count++;
            }
        }
    }
    closedir(procdir);
    return count;
}

int is_pid_dir(const struct dirent *entry) {
    if (entry->d_name[0] == '.') return 0;
    unsigned char dtype = entry->d_type;
    if (dtype != DT_UNKNOWN && dtype != DT_DIR) return 0;
    int len = strlen(entry->d_name);
    if (len < 1 || len > 10) return 0;
    for (int i = 0; i < len; i++) {
        if (!isdigit((unsigned char)entry->d_name[i])) return 0;
    }
    return 1;
}

int parse_stat(int pid, ProcessInfo *info) {
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    // Format: %d(name)%c%... skip to fields 14(utime),15(stime),22(starttime)
    // Use %*s %*d etc for skips (52 fields total, but partial)
    int success = fscanf(f,
        "%*d"        //1 pid
        " %*s"       //2 comm (in parens, tricky)
        " %*c"       //3 state
        " %*d %*d %*d %*d %*d"
        " %*d %*d %*d %*d %*d"
        " %*d %*d %*d %*d %*d"
        " %*d %*d %lu %lu"    //14 utime, 15 stime
        " %*d %*d %*d %*d %*d"
        " %*d %*d %*d %*d %*d"
        " %lu"       //22 starttime
        " %*[^ ]",   // skip rest
        &info->utime, &info->stime, &info->starttime) == 3;

    fclose(f);
    return success;
}

double get_uptime_secs(void) {
    FILE *f = fopen("/proc/uptime", "r");
    double uptime = 0.0;
    if (f) {
        fscanf(f, "%lf", &uptime);
        fclose(f);
    }
    return uptime;
}

void print_all_info(void) {
    // Sort by PID
    for (int i = 0; i < num_processes - 1; i++) {
        for (int j = 0; j < num_processes - i - 1; j++) {
            if (processes[j].pid > processes[j+1].pid) {
                ProcessInfo temp = processes[j];
                processes[j] = processes[j+1];
                processes[j+1] = temp;
            }
        }
    }
    // Print first 20 + summary
    printf("\nSample (first 20):\n");
    printf("PID\tUptime(s)\tCPU(s)\n");
    for (int i = 0; i < num_processes && i < 20; i++) {
        printf("%d\t%.1f\t\t%.3f\n", processes[i].pid, processes[i].uptime_secs, processes[i].cpu_secs);
    }
}
