#include <stdio.h> 
#include <string.h> 
#include <fcntl.h> 
#include <sys/stat.h> 
#include <sys/types.h> 
#include <sys/select.h>
#include <unistd.h> 
#include <stdlib.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include "include/constants.h"

// Server process:
// - Receives updates from UI/Keyboard/Drone/Obstacles/Targets.
// - Maintains a shared `struct data` and forwards updates to other processes.
//
// NOTE: master.c currently passes the pipe FD string as argv[0] (not argv[1])
// because execvp is called with arg_list[0] = pipeargs.

static sem_t *LOGsem;
static int centered = 0;

// Append one line to the logfile (protected by LOGsem).
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

enum {
    PIPE_UI = 0,
    PIPE_KEYBOARD,
    PIPE_DRONE,
    PIPE_OBSTACLES,
    PIPE_TARGETS,
    REC_PIPE_COUNT
};

int main(int argc, char *argv[]) {
    (void)argc;
    char msg[100];  // the variable for creating logging msgs


    // open the semaphore for logging
    LOGsem = sem_open(LOGSEMPATH, O_RDWR, 0666); // Initial value is 1
    if (LOGsem == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
    sem_post(LOGsem);

    // declaration of variables
    struct data data, updated_data;

    int server_UI[2];
    int server_keyboard[2];
    int server_drone[2];
    int server_obstacles[2];
    int server_targets[2];

    int rec_pipes[REC_PIPE_COUNT][2];
    // rec_pipes in order: UI, KEYBOARD, DRONE, OBSTACLES, TARGETS

        sscanf(argv[0], "%d %d|%d %d|%d %d|%d %d|%d %d|%d %d|%d %d|%d %d|%d %d|%d %d",
            &rec_pipes[PIPE_UI][0], &rec_pipes[PIPE_UI][1], &server_UI[0], &server_UI[1],
            &rec_pipes[PIPE_KEYBOARD][0], &rec_pipes[PIPE_KEYBOARD][1], &server_keyboard[0], &server_keyboard[1],
            &rec_pipes[PIPE_DRONE][0], &rec_pipes[PIPE_DRONE][1], &server_drone[0], &server_drone[1],
            &rec_pipes[PIPE_OBSTACLES][0], &rec_pipes[PIPE_OBSTACLES][1], &server_obstacles[0], &server_obstacles[1],
            &rec_pipes[PIPE_TARGETS][0], &rec_pipes[PIPE_TARGETS][1], &server_targets[0], &server_targets[1]);
    
    // Close unnecessary pipes
    close(server_drone[0]); 
    close(server_keyboard[0]);
    close(server_UI[0]);
    close(server_obstacles[0]);
    close(server_targets[0]);

    // Close write ends of the receiving pipes (server only reads from these).
    for (int i = 0; i < REC_PIPE_COUNT; i++) {
        close(rec_pipes[i][1]);
    }

    while (1) {
        fd_set reading;
        FD_ZERO(&reading);

        int max_pipe_fd = -1;
        for (int i = 0; i < REC_PIPE_COUNT; i++) {
            FD_SET(rec_pipes[i][0], &reading);
            if (rec_pipes[i][0] > max_pipe_fd) {
                max_pipe_fd = rec_pipes[i][0];
            }
        }

        // selecting which pipe is receiving data
        int ret_val = select(max_pipe_fd + 1, &reading, NULL, NULL, NULL);
        if (ret_val == -1) {
            perror("select");
            exit(EXIT_FAILURE);
        }

        if (ret_val <= 0) {
            continue;
        }

        for (int j = 0; j < REC_PIPE_COUNT; j++) {
            if (!FD_ISSET(rec_pipes[j][0], &reading)) {
                continue;
            }

            read(rec_pipes[j][0], &updated_data, sizeof(updated_data));
            switch (j) {
                case PIPE_UI: // UI
                    memcpy(data.max, updated_data.max, sizeof(updated_data.max));
                    if (!centered) {
                        memcpy(data.drone_pos, updated_data.drone_pos, sizeof(updated_data.drone_pos));
                        data.Cobs_touching = updated_data.Cobs_touching;
                        logit("[Server]:centered pos recived and sent to drone and target");
                        centered = 1;
                        write(server_targets[1], &data, sizeof(data));
                    }
                    logit("[Server]: max updated");
                    write(server_drone[1], &data, sizeof(data));
                    break;

                case PIPE_KEYBOARD: // keyboard
                    if (centered) {
                        data.key = updated_data.key;
                        write(server_drone[1], &data, sizeof(data));
                        sprintf(msg, "[Server]: key %c recieved and sent to Drone", data.key);
                        logit(msg);
                    }
                    break;

                case PIPE_DRONE: // drone
                    memcpy(data.drone_pos, updated_data.drone_pos, sizeof(updated_data.drone_pos));
                    sprintf(msg, "[Server]: New pos drone sent to UI %f,%f", data.drone_pos[0], data.drone_pos[1]);
                    logit(msg);
                    data.Cobs_touching = updated_data.Cobs_touching;

                    write(server_UI[1], &data, sizeof(data));
                    write(server_targets[1], &data, sizeof(data));
                    break;

                case PIPE_OBSTACLES: // obstacles
                    logit("[Server]: obstacles received ");
                    memcpy(data.obstacles, updated_data.obstacles, sizeof(updated_data.obstacles));
                    write(server_obstacles[1], &data, sizeof(data));
                    write(server_drone[1], &data, sizeof(data));
                    write(server_UI[1], &data, sizeof(data));
                    break;

                case PIPE_TARGETS: // targets
                    logit("[Server]: Targets received ");
                    memcpy(data.targets, updated_data.targets, sizeof(updated_data.targets));
                    data.targetReached = updated_data.targetReached;
                    write(server_obstacles[1], &data, sizeof(data));
                    write(server_UI[1], &data, sizeof(data));
                    break;

                default:
                    break;
            }
        }
    }


    // clean up
    close(server_drone[1]);
    close(server_keyboard[1]);
    close(server_UI[1]);
    close(server_obstacles[1]);
    close(server_targets[1]);
    for (int i = 0; i < REC_PIPE_COUNT; i++) {
        close(rec_pipes[i][0]);
    }


    return 0;
}
