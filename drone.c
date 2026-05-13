#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include "include/constants.h"
#include <math.h>

// Drone process:
// - Receives the shared `struct data` from the server.
// - Updates drone position using an Euler-like discrete update.
// - Applies repulsive forces when near obstacles.
// - Sends updated position back to the server.
//
// NOTE: master.c currently passes the pipe FD string as argv[0] (not argv[1])
// because execvp is called with arg_list[0] = pipeargs.

static double forceX = 0.0, forceY = 0.0;
static double repfx = 0.0, repfy = 0.0;
static int exit_flag = 0;
static struct data data;
static sem_t *LOGsem;
static char msg[100];

static int drone_server[2], server_drone[2];

static void logit(const char *msg) {
    sem_wait(LOGsem);

    FILE *file = fopen(LOGPATH, "a");
    if (file == NULL) {
        fprintf(stderr, "Error opening the file.\n");
        exit(EXIT_FAILURE);
    }

    // Write the string to the file
    fprintf(file, "%s\n", msg);
    // Close the file
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

static void parse_pipes(const char *arg) {
    sscanf(arg, "%d %d|%d %d", &drone_server[0], &drone_server[1], &server_drone[0], &server_drone[1]);
    close(drone_server[0]);
    close(server_drone[1]);
}

static void apply_key_forces(char key) {
    switch (key) {
        case 'w':
            forceY = forceY + FORCEY;
            break;
        case 'a':
            forceX = forceX - FORCEX;
            break;
        case 's':
            forceY = forceY - FORCEY;
            break;
        case 'd':
            forceX = forceX + FORCEX;
            break;
        case 'q':
            forceX = forceX - FORCEX;
            forceY = forceY + FORCEY;
            break;
        case 'e':
            forceX = forceX + FORCEX;
            forceY = forceY + FORCEY;
            break;
        case 'z':
            forceX = forceX - FORCEX;
            forceY = forceY - FORCEY;
            break;
        case 'c':
            forceX = forceX + FORCEX;
            forceY = forceY - FORCEY;
            break;
        case 'x':
            forceX = 0;
            forceY = 0;
            break;
        case 27: // ESC
            exit_flag = 1;
            break;
        default:
            break;
    }
}

static void clamp_to_bounds(double *x, double *y) {
    if (*x >= data.max[0]) {
        *x = data.max[0] - 0.5;
        forceX = 0;
    } else if (*x <= 0) {
        *x = 0.5;
        forceX = 0;
    }

    if (*y >= data.max[1]) {
        *y = data.max[1] - 0.5;
        forceY = 0;
    } else if (*y <= 0) {
        *y = 0.5;
        forceY = 0;
    }
}

static void apply_obstacle_repulsion(const double position[6]) {
    double sumx = 0, sumy = 0;

    for (int i = 0; i < NUM_OBSTACLES; i++) {
        double ox = data.obstacles[i * 2];
        double oy = data.obstacles[i * 2 + 1];

        double dx = data.drone_pos[0] - ox;
        double dy = data.drone_pos[1] - oy;
        double distance = sqrt(dx * dx + dy * dy);

        // Angle from obstacle to drone.
        double angle = atan2(dy, dx);

        sprintf(msg, "distance %f", distance);
        logit(msg);

        // If it reaches an obstacle, calculate the repulsive force.
        if (distance <= THRESH_TOUCHOBS) {
            data.Cobs_touching += 1;

            // NOTE: The repulsive force expression uses THRESH_TOUCH (not THRESH_TOUCHOBS)
            // because that is what the original implementation used.
            double rep_mag = pow((1 / distance) - (1 / THRESH_TOUCH), 2);

            if (ox > position[4] && forceX > 0) {
                sumx -= rep_mag * cos(angle);
            } else if (ox < position[4] && forceX < 0) {
                sumx += rep_mag * cos(angle);
            } else if (oy > position[5] && forceY > 0) {
                sumy -= rep_mag * sin(angle);
            } else if (oy < position[5] && forceY < 0) {
                sumy += rep_mag * sin(angle);
            }

            repfx = -0.5 * N * sumx;
            repfy = -0.5 * N * sumy;

            if (forceX > 0) {
                forceX -= fabs(repfx);
            } else if (forceX < 0) {
                forceX += fabs(repfx);
            }

            if (forceY > 0) {
                forceY -= fabs(repfy);
            } else if (forceY < 0) {
                forceY += fabs(repfy);
            }

            write(drone_server[1], &data, sizeof(data));
            sprintf(msg, "[Drone]: Drone touched obstacle %d times, this one %d", data.Cobs_touching, i * 2);
            logit(msg);
        }
    }
}

// a function to update the position according to Euler's formula
static void calc_position(char key, double position[6]) {
    apply_key_forces(key);
    apply_obstacle_repulsion(position);

    // Calculate new positions using the Euler-like discrete update.
    double newX = (forceX * T * T - M * position[4] + 2 * M * position[2] + K * T * position[0]) / (M + K * T);
    double newY = (forceY * T * T - M * position[5] + 2 * M * position[3] + K * T * position[1]) / (M + K * T);

    clamp_to_bounds(&newX, &newY);

    // Shift history: older <- previous <- current <- new
    position[4] = position[2];
    position[5] = position[3];
    position[2] = position[0];
    position[3] = position[1];
    position[0] = newX;
    position[1] = newY;
}

int main(int argc, char *argv[]) {
    (void)argc;
    open_logging_semaphore();
    parse_pipes(argv[0]);

    while (exit_flag == 0) {
        read(server_drone[0], &data, sizeof(data));
        calc_position(data.key, data.drone_pos);
        sprintf(msg, "[drone]: key %c received, new pos: %f,%f", data.key, data.drone_pos[0], data.drone_pos[1]);
        logit(msg);
        write(drone_server[1], &data, sizeof(data));
    }

    // Cleanup
    close(server_drone[0]);
    close(drone_server[1]);
    sem_close(LOGsem);

    return 0;
}
