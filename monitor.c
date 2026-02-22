#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>

// Helper function to check if a string contains only digits
int is_numeric(const char *str) {
    if (str == NULL || *str == '\0') {
        return 0;
    }
    for (int i = 0; str[i] != '\0'; i++) {
        if (!isdigit((unsigned char)str[i])) {
            return 0; // Contains a non-digit character
        }
    }
    return 1; // Contains only digits
}

int main() {
    DIR *proc_dir;
    struct dirent *entry;

    // 1. Open the /proc directory
    proc_dir = opendir("/proc");
    if (proc_dir == NULL) {
        perror("Failed to open /proc");
        return EXIT_FAILURE;
    }

    printf("Active Process IDs:\n");
    printf("-------------------\n");

    // 2. Iterate through the directory entries
    while ((entry = readdir(proc_dir)) != NULL) {
        // 3. Filter entries: we only want directories that are purely numerical
        // Note: checking entry->d_type == DT_DIR ensures it's a directory, 
        // which is standard for procfs PID entries.
        if (entry->d_type == DT_DIR && is_numeric(entry->d_name)) {
            printf("%s\n", entry->d_name);
        }
    }

    // 4. Clean up
    closedir(proc_dir);

    return EXIT_SUCCESS;
}
