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
#include <limits.h>
#include <time.h>
#include <signal.h>    // sigaction
#include <sys/time.h>  // gettimeofday

// Constants
#define MAX_PATH 256
#define MAX_PIDS 20000
#define MAX_USERS 1000
#define BUFFER_SIZE 4096
#define LOOP_INTERVAL 1.0

#ifndef DT_DIR
#define DT_DIR 4
#endif
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif

typedef struct {
    int pid;
    uid_t uid;
    char username[64];  // fixed size for pw_name
    double prev_cpu_secs;
    double total_cpu_delta;  // since monitor start
} ProcessInfo;

typedef struct {
    uid_t uid;
    char name[64];
    double total_cpu_ms;
} UserTotal;

ProcessInfo *current_processes;
int num_current = 0;
UserTotal *users;
int num_users = 0;
double monitor_start_time;
long clk_tck;

// Prototypes
int enumerate_and_parse(void);
int is_pid_dir(const struct dirent *entry);
int parse_stat(int pid, double *utime, double *stime, double *starttime);
int get_uid_username(int pid, uid_t *uid, char *name);
double get_uptime_secs(void);
void compute_deltas(void);
void print_ranking(void);
void cleanup(int sig);
void init_users(void);

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

    current_processes = malloc(MAX_PIDS * sizeof(ProcessInfo));
    users = malloc(MAX_USERS * sizeof(UserTotal));
    if (!current_processes || !users) {
        perror("malloc");
        return 1;
    }

    // Setup cleanup
    signal(SIGINT, cleanup);

    monitor_start_time = get_uptime_secs();
    printf("Monitor started at %.2f secs uptime. Duration: %ds\n", monitor_start_time, duration);

    init_users();

    // First sample (prev_cpu_secs = 0 for new procs)
    printf("Sample 1/%d: ", duration);
    enumerate_and_parse();
    compute_deltas();

    // Loop every second
    for (int i = 1; i < duration; i++) {
        sleep(1);
        printf("\rSample %d/%d: %d procs", i+1, duration, num_current);
        fflush(stdout);
        enumerate_and_parse();
        compute_deltas();
    }
    printf("\n");

    print_ranking();

    cleanup(0);
    return 0;
}

int enumerate_and_parse(void) {
    DIR *procdir = opendir("/proc");
    if (!procdir) return 0;

    num_current = 0;
    struct dirent *entry;
    while ((entry = readdir(procdir)) && num_current < MAX_PIDS) {
        if (!is_pid_dir(entry)) continue;

        int pid = (int)strtol(entry->d_name, NULL, 10);
        double utime, stime, starttime;
        if (!parse_stat(pid, &utime, &stime, &starttime)) continue;

        // Ignore processes started before monitor
        double proc_uptime = get_uptime_secs() - (starttime / clk_tck);
        if (proc_uptime + LOOP_INTERVAL < get_uptime_secs() - monitor_start_time) continue;

        current_processes[num_current].pid = pid;
        get_uid_username(pid, &current_processes[num_current].uid, current_processes[num_current].username);
        current_processes[num_current].prev_cpu_secs = (utime + stime) / clk_tck;
        current_processes[num_current].total_cpu_delta = 0.0;
        num_current++;
    }
    closedir(procdir);
    return num_current;
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

int parse_stat(int pid, double *utime, double *stime, double *starttime) {
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    int success = fscanf(f,
        "%*d %*s %*c %*d%*d%*d%*d%*d %*d%*d%*d%*d%*d %*d%*d%*d%*d%*d %lf %lf %*d%*d%*d%*d%*d %*d%*d%*d%*d%*d %lf %*[^\n]",
        utime, stime, starttime) == 3;

    fclose(f);
    return success;
}

int get_uid_username(int pid, uid_t *uid, char *name) {
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    *uid = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            sscanf(line, "Uid:%*d\t%u", uid);  // real uid (first after tab)
            break;
        }
    }
    fclose(f);

    // Get username
    struct passwd pw, tmp;
    char buf[1024];
    if (getpwuid_r(*uid, &pw, buf, sizeof(buf), &tmp) == 0) {
        strncpy(name, pw.pw_name, 63);
        name[63] = '\0';
    } else {
        snprintf(name, 64, "%u", *uid);
    }
    return 1;
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

void compute_deltas(void) {
    double now_cpu;
    for (int i = 0; i < num_current; i++) {
        ProcessInfo *p = &current_processes[i];
        now_cpu = p->prev_cpu_secs;  // updated in parse? Wait no, parse sets prev

        // Delta since prev sample (or start)
        double delta = now_cpu - p->prev_cpu_secs;
        if (delta < 0) delta = 0;  // wrapped or error
        p->total_cpu_delta += delta;

        // Aggregate to user
        for
