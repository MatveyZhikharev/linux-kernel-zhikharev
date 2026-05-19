#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>

#define MAX_LINE 512
#define MAX_PROCESSES 256

struct process_info {
    int pid;
    char comm[256];
    char state;
    long vsize;
    long rss;
    unsigned long utime;
    unsigned long stime;
};

struct system_info {
    unsigned long user;
    unsigned long nice;
    unsigned long system;
    unsigned long idle;
    unsigned long iowait;
    unsigned long irq;
    unsigned long softirq;
    long memtotal;
    long memfree;
    long memavailable;
};

int read_cpu_stat(struct system_info *info) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        perror("Cannot open /proc/stat");
        return -1;
    }
    
    char line[MAX_LINE];
    if (!fgets(line, MAX_LINE, fp)) {
        fclose(fp);
        return -1;
    }
    
    int ret = sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu",
                     &info->user, &info->nice, &info->system,
                     &info->idle, &info->iowait, &info->irq,
                     &info->softirq);
    
    fclose(fp);
    return (ret == 7) ? 0 : -1;
}

int read_meminfo(struct system_info *info) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        perror("Cannot open /proc/meminfo");
        return -1;
    }
    
    char line[MAX_LINE];
    while (fgets(line, MAX_LINE, fp)) {
        if (sscanf(line, "MemTotal: %ld", &info->memtotal) == 1) continue;
        if (sscanf(line, "MemFree: %ld", &info->memfree) == 1) continue;
        if (sscanf(line, "MemAvailable: %ld", &info->memavailable) == 1) break;
    }
    
    fclose(fp);
    return 0;
}

int count_processes(void) {
    DIR *dir = opendir("/proc");
    if (!dir) {
        perror("Cannot open /proc");
        return -1;
    }
    
    int count = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && entry->d_name[0] >= '0' && entry->d_name[0] <= '9') {
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

int read_processes(struct process_info *procs, int max_count) {
    DIR *dir = opendir("/proc");
    if (!dir) {
        perror("Cannot open /proc");
        return -1;
    }
    
    int count = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL && count < max_count) {
        if (entry->d_type != DT_DIR) continue;
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        
        int pid = atoi(entry->d_name);
        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        
        FILE *fp = fopen(path, "r");
        if (!fp) continue;
        
        char comm[256];
        char state;
        long dummy;
        unsigned long utime, stime;
        
        int ret = fscanf(fp, "%d %s %c %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %lu %lu",
                        &procs[count].pid, comm, &state, &dummy, &dummy,
                        &dummy, &dummy, &dummy, &dummy, &dummy, &dummy,
                        &dummy, &dummy, &utime, &stime);
        
        if (ret >= 15) {
            procs[count].pid = pid;
            snprintf(procs[count].comm, sizeof(procs[count].comm), "%s", comm);
            procs[count].state = state;
            procs[count].utime = utime;
            procs[count].stime = stime;
            count++;
        }
        
        fclose(fp);
    }
    
    closedir(dir);
    return count;
}

void print_cpu_usage(struct system_info *info) {
    unsigned long total = info->user + info->nice + info->system + info->idle +
                         info->iowait + info->irq + info->softirq;
    
    if (total == 0) total = 1;
    
    double user_pct = (double)(info->user) * 100.0 / total;
    double system_pct = (double)(info->system) * 100.0 / total;
    double idle_pct = (double)(info->idle) * 100.0 / total;
    double iowait_pct = (double)(info->iowait) * 100.0 / total;
    
    printf("\n=== CPU STATISTICS ===\n");
    printf("User:   %.1f%%\n", user_pct);
    printf("System: %.1f%%\n", system_pct);
    printf("IOWait: %.1f%%\n", iowait_pct);
    printf("Idle:   %.1f%%\n", idle_pct);
}

void print_memory_usage(struct system_info *info) {
    printf("\n=== MEMORY STATISTICS ===\n");
    printf("Total:     %ld KB\n", info->memtotal);
    printf("Free:      %ld KB\n", info->memfree);
    printf("Available: %ld KB\n", info->memavailable);
    
    if (info->memtotal > 0) {
        double used_pct = (double)(info->memtotal - info->memavailable) * 100.0 / info->memtotal;
        printf("Used:      %.1f%%\n", used_pct);
    }
}

void print_processes(struct process_info *procs, int count) {
    printf("\n=== RUNNING PROCESSES (TOP 10) ===\n");
    printf("%-8s %-40s %s\n", "PID", "COMMAND", "S");
    printf("----------------------------------------\n");
    
    int printed = 0;
    for (int i = 0; i < count && printed < 10; i++) {
        printf("%-8d %-40s %c\n", procs[i].pid, procs[i].comm, procs[i].state);
        printed++;
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║    PROCFS SYSTEM MONITOR v1.0          ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("Time: %s", asctime(timeinfo));
    
    struct system_info sysinfo = {0};
    struct process_info procs[MAX_PROCESSES];
    
    if (read_cpu_stat(&sysinfo) < 0) {
        fprintf(stderr, "Error reading CPU statistics\n");
        return 1;
    }
    
    if (read_meminfo(&sysinfo) < 0) {
        fprintf(stderr, "Error reading memory information\n");
        return 1;
    }
    
    print_cpu_usage(&sysinfo);
    print_memory_usage(&sysinfo);
    
    int proc_count = read_processes(procs, MAX_PROCESSES);
    if (proc_count > 0) {
        printf("\nTotal Processes: %d\n", proc_count);
        print_processes(procs, proc_count);
    }
    
    printf("\n════════════════════════════════════════\n\n");
    
    return 0;
}
