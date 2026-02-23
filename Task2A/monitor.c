#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <pwd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

// Constants
#define MAX_PATH 256
#define MAX_PIDS 32768
#define MAX_USERS 1024

// Data Structures
typedef struct {
    int pid;
    unsigned long long starttime;
    unsigned long long last_cpu_ticks;
    int active_this_tick;
} ProcessRecord;

typedef struct {
    uid_t uid;
    double total_cpu_ms;
} UserRecord;

// Global State
ProcessRecord *tracked_procs;
int num_tracked = 0;
UserRecord *users;
int num_users = 0;
long clk_tck;
double monitor_start_uptime;
int keep_running = 1;

// Prototypes
void cleanup(int sig);
double get_uptime_secs(void);
int is_pid_dir(const struct dirent *entry);
int parse_stat(int pid, unsigned long long *utime, unsigned long long *stime, unsigned long long *starttime);
int get_uid(int pid, uid_t *uid);
void add_to_user(uid_t uid, double ms);
int compare_users(const void *a, const void *b);
void print_ranking(void);

int main(int argc, char *argv[]) {
    // 1. Parse Arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <seconds>\n", argv[0]);
        return 1;
    }
    int duration = atoi(argv[1]);
    if (duration <= 0) {
        fprintf(stderr, "Duration must be positive\n");
        return 1;
    }

    // Initialize system clock ticks per second
    clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) {
        perror("sysconf CLK_TCK");
        return 1;
    }

    // Allocate memory
    tracked_procs = malloc(MAX_PIDS * sizeof(ProcessRecord));
    users = malloc(MAX_USERS * sizeof(UserRecord));
    if (!tracked_procs || !users) {
        perror("malloc");
        return 1;
    }
    memset(users, 0, MAX_USERS * sizeof(UserRecord));

    // Handle interrupts gracefully
    signal(SIGINT, cleanup);

    monitor_start_uptime = get_uptime_secs();

    // 2. Monitoring Loop
    for (int tick = 0; tick < duration && keep_running; tick++) {
        DIR *procdir = opendir("/proc");
        if (!procdir) {
            perror("opendir(/proc)");
            break;
        }

        // Mark all tracked processes as inactive initially for this tick
        for (int i = 0; i < num_tracked; i++) {
            tracked_procs[i].active_this_tick = 0;
        }

        struct dirent *entry;
        while ((entry = readdir(procdir)) != NULL) {
            if (!is_pid_dir(entry)) continue;

            int pid = atoi(entry->d_name);
            unsigned long long utime, stime, starttime;
            uid_t uid;

            // Read CPU usage and UID
            if (!parse_stat(pid, &utime, &stime, &starttime)) continue;
            if (!get_uid(pid, &uid)) continue;

            unsigned long long total_ticks = utime + stime;
            double proc_start_sec = (double)starttime / clk_tck;

            // Check if we are already tracking this process
            int found_idx = -1;
            for (int i = 0; i < num_tracked; i++) {
                if (tracked_procs[i].pid == pid && tracked_procs[i].starttime == starttime) {
                    found_idx = i;
                    break;
                }
            }

            if (found_idx >= 0) {
                // Existing process: compute delta
                unsigned long long delta_ticks = total_ticks - tracked_procs[found_idx].last_cpu_ticks;
                if (delta_ticks > 0) {
                    double delta_ms = (double)delta_ticks * 1000.0 / clk_tck;
                    add_to_user(uid, delta_ms);
                }
                tracked_procs[found_idx].last_cpu_ticks = total_ticks;
                tracked_procs[found_idx].active_this_tick = 1;
            } else {
                // New process
                if (num_tracked < MAX_PIDS) {
                    ProcessRecord pr;
                    pr.pid = pid;
                    pr.starttime = starttime;
                    pr.active_this_tick = 1;

                    if (proc_start_sec < monitor_start_uptime) {
                        // Started before monitor: ignore past CPU time
                        pr.last_cpu_ticks = total_ticks;
                    } else {
                        // Started after monitor: count all current CPU time
                        pr.last_cpu_ticks = total_ticks;
                        double delta_ms = (double)total_ticks * 1000.0 / clk_tck;
                        add_to_user(uid, delta_ms);
                    }
                    tracked_procs[num_tracked++] = pr;
                }
            }
        }
        closedir(procdir);

        // Remove processes that terminated during this tick to save space
        int active_count = 0;
        for (int i = 0; i < num_tracked; i++) {
            if (tracked_procs[i].active_this_tick) {
                tracked_procs[active_count++] = tracked_procs[i];
            }
        }
        num_tracked = active_count;

        // Sleep until the next second
        if (tick < duration - 1) {
            sleep(1);
        }
    }

    // 3. Print Final Output
    print_ranking();
    
    // Cleanup
    free(tracked_procs);
    free(users);
    return 0;
}

// --- Helper Functions ---

void cleanup(int sig) {
    keep_running = 0;
    if (sig != 0) {
        printf("\nMonitor interrupted. Printing partial results...\n");
    }
}

double get_uptime_secs(void) {
    FILE *f = fopen("/proc/uptime", "r");
    double uptime = 0.0;
    if (f) {
        if (fscanf(f, "%lf", &uptime) != 1) uptime = 0.0;
        fclose(f);
    }
    return uptime;
}

int is_pid_dir(const struct dirent *entry) {
    if (entry->d_name[0] == '.') return 0;
    for (int i = 0; entry->d_name[i] != '\0'; i++) {
        if (!isdigit((unsigned char)entry->d_name[i])) return 0;
    }
    return 1;
}

int parse_stat(int pid, unsigned long long *utime, unsigned long long *stime, unsigned long long *starttime) {
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char buffer[4096];
    if (!fgets(buffer, sizeof(buffer), f)) {
        fclose(f);
        return 0;
    }
    fclose(f);

    // Safely skip the executable name which might contain spaces e.g., "123 (my process) S..."
    char *p = strrchr(buffer, ')');
    if (!p) return 0;
    p += 2; // Move past ") "

    // Read the space-separated fields after the executable name
    // utime is 12th, stime is 13th, starttime is 20th relative to the end of the name
    unsigned long long fields[25] = {0};
    char *tok = strtok(p, " ");
    int i = 0;
    while (tok && i < 25) {
        fields[i++] = strtoull(tok, NULL, 10);
        tok = strtok(NULL, " ");
    }

    if (i >= 20) {
        *utime = fields[11];
        *stime = fields[12];
        *starttime = fields[19];
        return 1;
    }
    return 0;
}

int get_uid(int pid, uid_t *uid) {
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            if (sscanf(line, "Uid:\t%u", uid) == 1) {
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

void add_to_user(uid_t uid, double ms) {
    for (int i = 0; i < num_users; i++) {
        if (users[i].uid == uid) {
            users[i].total_cpu_ms += ms;
            return;
        }
    }
    if (num_users < MAX_USERS) {
        users[num_users].uid = uid;
        users[num_users].total_cpu_ms = ms;
        num_users++;
    }
}

int compare_users(const void *a, const void *b) {
    UserRecord *uA = (UserRecord *)a;
    UserRecord *uB = (UserRecord *)b;
    if (uB->total_cpu_ms > uA->total_cpu_ms) return 1;
    if (uB->total_cpu_ms < uA->total_cpu_ms) return -1;
    return 0;
}

void print_ranking(void) {
    qsort(users, num_users, sizeof(UserRecord), compare_users);

    // The header exactly matches the assignment PDF
    // The Python script will naturally skip this line
    printf("Rank\tUser\tCPU Time (milliseconds)\n");
    
    for (int i = 0; i < num_users; i++) {
        if (users[i].total_cpu_ms > 0) {
            char username[64];
            struct passwd *pw = getpwuid(users[i].uid);
            if (pw) {
                strncpy(username, pw->pw_name, sizeof(username) - 1);
                username[sizeof(username) - 1] = '\0';
            } else {
                snprintf(username, sizeof(username), "%u", users[i].uid);
            }

            // MUST be: Rank (int) -> Username (string) -> CPU Time (int)
            printf("%d\t%s\t%llu\n", i + 1, username, (unsigned long long)users[i].total_cpu_ms);
        }
    }
}

