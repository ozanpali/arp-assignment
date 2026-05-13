#include <ncurses.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "include/constants.h"

// Watchdog process:
// - Receives the PIDs of all other game processes as command-line arguments.
// - Every second, checks whether each process is alive using `kill(pid, 0)`.
// - Displays status using ncurses and appends a line to the shared logfile.

// master.c starts watchdog with exactly these 6 PIDs:
//   server, UI, drone, keyboard, obstacles, targets
enum {
    PROC_SERVER = 0,
    PROC_UI,
    PROC_DRONE,
    PROC_KEYBOARD,
    PROC_OBSTACLES,
    PROC_TARGETS,
    PROC_COUNT
};

static void init_ncurses(void) {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    clear();
}

static sem_t *open_logging_semaphore(void) {
    sem_t *LOGsem = sem_open(LOGSEMPATH, O_RDWR, 0666);
    if (LOGsem == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
    return LOGsem;
}

static void update_status(const pid_t pids[PROC_COUNT], int alive[PROC_COUNT]) {
    for (int i = 0; i < PROC_COUNT; i++) {
        alive[i] = (kill(pids[i], 0) == 0);
    }
}

static void draw_status_screen(const int alive[PROC_COUNT]) {
    mvprintw(0, 0, "1 means the process is alive, 0 means it is not:");
    mvprintw(1, 0, "Server: %d", alive[PROC_SERVER]);
    mvprintw(2, 0, "UI: %d", alive[PROC_UI]);
    mvprintw(3, 0, "Keyboard: %d", alive[PROC_KEYBOARD]);
    mvprintw(4, 0, "Drone: %d", alive[PROC_DRONE]);
    mvprintw(5, 0, "Targets: %d", alive[PROC_TARGETS]);
    mvprintw(6, 0, "Obstacles: %d", alive[PROC_OBSTACLES]);
    refresh();
}

static void log_status_line(sem_t *LOGsem, const int alive[PROC_COUNT]) {
    char slog[160];
    snprintf(
        slog,
        sizeof(slog),
        "[watchdog] Server: %d , UI: %d, Keyboard: %d, Drone: %d, Targets: %d, Obstacles: %d",
        alive[PROC_SERVER],
        alive[PROC_UI],
        alive[PROC_KEYBOARD],
        alive[PROC_DRONE],
        alive[PROC_TARGETS],
        alive[PROC_OBSTACLES]
    );

    sem_wait(LOGsem);
    FILE *file = fopen(LOGPATH, "a");
    if (file == NULL) {
        fprintf(stderr, "Error opening the file.\n");
        exit(EXIT_FAILURE);
    }
    fprintf(file, "%s\n", slog);
    fclose(file);
    sem_post(LOGsem);
}

static bool all_alive(const int alive[PROC_COUNT]) {
    for (int i = 0; i < PROC_COUNT; ++i) {
        if (alive[i] == 0) {
            return false;
        }
    }
    return true;
}

int main(int argc, char const *argv[]) {
    // argv[0] = ./build/watchdog
    // argv[1..6] = 6 PIDs (server, UI, drone, keyboard, obstacles, targets)
    if (argc != PROC_COUNT + 1) {
        fprintf(
            stderr,
            "Usage: %s <server_pid> <ui_pid> <drone_pid> <keyboard_pid> <obstacles_pid> <targets_pid>\n",
            argv[0]
        );
        return EXIT_FAILURE;
    }

    pid_t pids[PROC_COUNT];
    pids[PROC_SERVER] = (pid_t)atoi(argv[1]);
    pids[PROC_UI] = (pid_t)atoi(argv[2]);
    pids[PROC_DRONE] = (pid_t)atoi(argv[3]);
    pids[PROC_KEYBOARD] = (pid_t)atoi(argv[4]);
    pids[PROC_OBSTACLES] = (pid_t)atoi(argv[5]);
    pids[PROC_TARGETS] = (pid_t)atoi(argv[6]);

    int alive[PROC_COUNT] = {0};

    init_ncurses();
    sem_t *LOGsem = open_logging_semaphore();

    while (1) {
        sleep(1);

        update_status(pids, alive);
        draw_status_screen(alive);
        log_status_line(LOGsem, alive);

        // If any process dies, exit watchdog so master can terminate the rest.
        if (!all_alive(alive)) {
            break;
        }
    }

    endwin();
    sem_close(LOGsem);
    return 0;
}
