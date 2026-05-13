#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <ncurses.h>
#include "include/constants.h"
#include <stdbool.h>
#include <time.h>

// UI process:
// - Uses ncurses to draw the current game state.
// - Receives updated `struct data` from the server via a pipe.
// - Sends window size and initial drone position to the server.
// - Logs game objects to a shared logfile protected by a named semaphore.
static sem_t* LOGsem;
static time_t start_time;

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

static void start_timer(void) {
    time(&start_time);
}

// Seconds elapsed since `start_timer()`.
static double get_elapsed_time(void) {
    time_t end_time;
    time(&end_time);
    return difftime(end_time, start_time);
}

// Configure ncurses for a full-screen, no-echo UI.
static void init_ncurses(void) {
    initscr();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);
}

// Open the named semaphore used to serialize access to the logfile.
static void open_logging_semaphore(void) {
    LOGsem = sem_open(LOGSEMPATH, O_RDWR, 0666);
    if (LOGsem == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
}

// Parse the pipe file descriptors passed by the master process and
// close the unused ends for this process.
static void parse_pipes(const char *arg, int UI_server[2], int server_UI[2]) {
    sscanf(arg, "%d %d|%d %d", &UI_server[0], &UI_server[1], &server_UI[0], &server_UI[1]);
    close(UI_server[0]);
    close(server_UI[1]);
}

// Initialize the first UI state and send it to the server (initial drone pos + window size).
static void init_data_and_send(struct data *data, int width, int height, int write_fd) {
    double position[6] = {
        width / 2.0, height / 2.0,
        width / 2.0, height / 2.0,
        width / 2.0, height / 2.0
    };

    memcpy(data->drone_pos, position, sizeof(position));
    data->max[0] = width;
    data->max[1] = height;
    data->Cobs_touching = 0;
    data->targetReached = 0;
    data->key = 0;
    write(write_fd, data, sizeof(*data));
    logit("[UI]: Window created and pos sent to server");
}

// If the terminal was resized, send the new dimensions to the server.
static void update_screen_size_if_needed(struct data *data, int write_fd) {
    int newHeight, newWidth;
    getmaxyx(stdscr, newHeight, newWidth);

    if (newHeight != data->max[1] || newWidth != data->max[0]) {
        data->max[1] = newHeight;
        data->max[0] = newWidth;
        write(write_fd, data, sizeof(*data));
    }
}

// True when all targets were marked as hit (server sets target entries to -1).
static bool all_targets_hit(const struct data *data) {
    for (int i = 0; i < NUM_TARGETS * 2; ++i) {
        if (data->targets[i] != -1) {
            return false;
        }
    }
    return true;
}

// Regular score update while the game is running.
static int compute_score(const struct data *data) {
    return (5 * NUM_TARGETS + (2 * NUM_OBSTACLES) + (3 * data->targetReached) - (data->Cobs_touching * 2) - (get_elapsed_time() * 1.5));
}

// Final score (uses the time snapshot taken at first GAME OVER).
static int compute_game_over_score(const struct data *data, double time_needed) {
    return (5 * NUM_TARGETS + (2 * NUM_OBSTACLES) + (3 * data->targetReached) - (data->Cobs_touching * 2) - (time_needed * 0.15));
}

// Draw top-left HUD values.
static void draw_hud(const struct data *data, int score) {
    mvprintw(1, 1, "X: %.2f", data->drone_pos[0]);
    mvprintw(2, 1, "Y: %.2f", data->drone_pos[1]);
    mvprintw(3, 1, "score: %d", score);
}

// Draw the drone at its current coordinates.
static void draw_drone(const struct data *data) {
    mvprintw((int)data->drone_pos[1], (int)data->drone_pos[0], "+");
}

// Draw targets and log their coordinates.
static void draw_targets_and_log(const struct data *data) {
    char msg[100];
    for (int i = 0; i < NUM_TARGETS * 2; i += 2) {
        int x = (int)data->targets[i];
        int y = (int)data->targets[i + 1];

        mvprintw(y, x, "%d", i / 2);

        sprintf(msg, "[UI]: TARGET x[%d]: %d, y[%d]: %d", i / 2, x, i / 2, y);
        logit(msg);
    }
}

// Draw obstacles and log their coordinates.
static void draw_obstacles_and_log(const struct data *data) {
    char msg[100];
    for (int i = 0; i < NUM_OBSTACLES * 2; i += 2) {
        int x = (int)data->obstacles[i];
        int y = (int)data->obstacles[i + 1];

        mvprintw(y, x, "X");

        sprintf(msg, "[UI]: OBSTACLE x[%d]: %d, y[%d]: %d", i / 2, x, i / 2, y);
        logit(msg);
    }
}

// Draw the GAME OVER message centered-ish in the window.
static void draw_game_over(const struct data *data, int score) {
    mvprintw((int)(data->max[0] / 2), (int)(data->max[1] / 2 - 8), "GAME OVER, SCORE: %d", score);
    refresh();
}


int main(int argc, char const *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pipes>\n", argv[0]);
        return EXIT_FAILURE;
    }

    init_ncurses();
    open_logging_semaphore();


    // PIPES
    int UI_server[2], server_UI[2];
    parse_pipes(argv[1], UI_server, server_UI);

    // `struct data` used to be a global (therefore zero-initialized).
    // Keep the same behavior by explicitly zeroing it here.
    struct data data = (struct data){0};
    bool game_over = false;
    double time_needed = 0;

    int height, width;
    getmaxyx(stdscr, height, width);
    init_data_and_send(&data, width, height, UI_server[1]);
    start_timer();
    
    while (1) {
        do {
            read(server_UI[0], &data, sizeof(data)); // recieve the updated data
        } while (data.obstacles[0] == 0);

        int Score = compute_score(&data);

        // Clear the screen
        clear();

        update_screen_size_if_needed(&data, UI_server[1]);
        // Draw the bordered box
        box(stdscr, 0, 0);

        if (all_targets_hit(&data)) {
            // All targets hit, print GAME OVER and score
            if (!game_over) {
                time_needed = get_elapsed_time();
            }
            Score = compute_game_over_score(&data, time_needed);
            draw_game_over(&data, Score);
            game_over = true;
        } else {
            draw_drone(&data);
            draw_hud(&data, Score);
            draw_targets_and_log(&data);
            draw_obstacles_and_log(&data);

            // Refresh the screen
            refresh();
        }
    }

    // End ncurses
    endwin();

    close(server_UI[1]);
    close(UI_server[0]); 

    return 0;
}
