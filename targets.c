#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <ncurses.h>
#include <time.h>
#include "include/constants.h"
#include <math.h>

// Targets process:
// - Creates NUM_TARGETS random target positions.
// - On each update from the server, checks if the drone has reached the next active target.
// - Marks the reached target as (-1, -1) and forwards the update back to the server.
//
// NOTE: master.c currently passes the pipe FD string as argv[0] (not argv[1])
// because execvp is called with arg_list[0] = pipeargs.

static sem_t* LOGsem;
static struct data data;
static int targetReached = 0;

static void logit(const char *msg) {
    sem_wait(LOGsem);

    FILE *file = fopen(LOGPATH, "a");
    if (file == NULL) {
        fprintf(stderr, "Error opening the file.\n");
        exit(EXIT_FAILURE);
    }

    fprintf(file, "%s\n", msg);
    fclose(file);
    sem_post(LOGsem);
}

static void open_logging_semaphore(void) {
    LOGsem = sem_open(LOGSEMPATH, O_RDWR, 0666);
    if (LOGsem == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
}

static void parse_pipes(const char *arg, int targets_server[2], int server_targets[2]) {
    sscanf(arg, "%d %d|%d %d", &targets_server[0], &targets_server[1], &server_targets[0], &server_targets[1]);
    close(targets_server[0]);
    close(server_targets[1]);
}

static void create_targets(void) {
    char msg[100];
    for (int i = 0; i < NUM_TARGETS * 2; i += 2) {
        do {
            data.targets[i] = rand() % (int)data.max[0];
        } while (fabs(data.targets[i] - data.drone_pos[0]) < THRESH_TARGET);

        do {
            data.targets[i + 1] = rand() % (int)data.max[1];
        } while (fabs(data.targets[i + 1] - data.drone_pos[1]) < THRESH_TARGET);

        sprintf(msg, "[Targets]: x[%d]: %f, y[%d]: %f", i / 2, data.targets[i], i / 2, data.targets[i + 1]);
        logit(msg);
    }
}

// Returns index [0..NUM_TARGETS-1] of the next active target, or -1 if none remain.
static int find_next_active_target(int start_index) {
    for (int i = start_index; i < NUM_TARGETS; i++) {
        if (data.targets[i * 2] != -1) {
            return i;
        }
    }
    return -1;
}

static double distance_to_target(int target_index) {
    double dx = data.drone_pos[0] - data.targets[target_index * 2];
    double dy = data.drone_pos[1] - data.targets[target_index * 2 + 1];
    return sqrt(dx * dx + dy * dy);
}

int main(int argc, char *argv[]) {
    (void)argc;
    open_logging_semaphore();

    // PIPES
    int targets_server[2], server_targets[2];
    parse_pipes(argv[0], targets_server, server_targets);

    logit("[Targets]: ALIVE");

    // Receive initial state (window size + initial drone position).
    read(server_targets[0], &data, sizeof(data));

    // Create targets and send them to the server.
    create_targets();
    write(targets_server[1], &data, sizeof(data));

    while (1) {
        read(server_targets[0], &data, sizeof(data));

        // Find the next active target starting from the last known index.
        int next_target = find_next_active_target(targetReached);
        if (next_target < 0) {
            // No targets left; server/UI will show game over.
            continue;
        }
        targetReached = next_target;

        double distance = distance_to_target(targetReached);
        if (distance < THRESH_TOUCH) {
            // Keep a copy for logging before overwriting with (-1, -1).
            double hit_x = data.targets[targetReached * 2];
            double hit_y = data.targets[targetReached * 2 + 1];

            // Drone has reached the target, update the target status.
            data.targets[targetReached * 2] = -1;
            data.targets[targetReached * 2 + 1] = -1;

            data.targetReached = targetReached;
            write(targets_server[1], &data, sizeof(data));

            char msg[100];
            sprintf(msg, "[Targets]: Drone reached target %d at (%f, %f)", targetReached, hit_x, hit_y);
            logit(msg);
        }
    }

    // cleanup (unreachable with current infinite loop)
    close(targets_server[1]);
    close(server_targets[0]);
    sem_close(LOGsem);
    return 0;
}