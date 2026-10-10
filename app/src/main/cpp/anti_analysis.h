#ifndef ANTI_ANALYSIS_H
#define ANTI_ANALYSIS_H

#include <pthread.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <android/log.h>

#define TAG "HidimbiNative"

static volatile int g_analysis_detected = 0;

inline void check_maps() {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return;

    char line[512];
    const char* bad_patterns[] = {
        "frida", "gadget", "xposed", "substrate", "gdbserver", "r2frida"
    };

    while (fgets(line, sizeof(line), fp)) {
        for (int i = 0; i < 6; i++) {
            if (strstr(line, bad_patterns[i]) != NULL) {
                g_analysis_detected = 1;
                fclose(fp);
                return;
            }
        }
    }
    fclose(fp);
}

inline void* watchdog_thread(void* arg) {
    // 1. Initial ptrace attach test
    if (ptrace(PTRACE_TRACEME, 0, 1, 0) < 0) {
        g_analysis_detected = 1;
    }

    // 2. Periodic monitoring loop
    while (1) {
        check_maps();
        usleep(500000); // 500ms
    }
    return NULL;
}

inline void init_anti_analysis() {
    pthread_t t;
    pthread_create(&t, NULL, watchdog_thread, NULL);
    pthread_detach(t);
}

#endif // ANTI_ANALYSIS_H
