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

// Keyboard process:
// - Reads key presses from ncurses (non-blocking).
// - Sends the latest key to the server via a pipe.
// - Appends key events to the shared logfile protected by a named semaphore.

static void init_ncurses(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
}

static sem_t *open_logging_semaphore(void) {
    sem_t *LOGsem = sem_open(LOGSEMPATH, O_RDWR, 0666);
    if (LOGsem == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
    return LOGsem;
}

static void parse_pipes(const char *arg, int keyboard_server[2], int server_keyboard[2]) {
    sscanf(arg, "%d %d|%d %d", &keyboard_server[0], &keyboard_server[1], &server_keyboard[0], &server_keyboard[1]);
    close(keyboard_server[0]);
    close(server_keyboard[1]);
}

static void log_key(sem_t *LOGsem, int keyPressed, char sentKey) {
    sem_wait(LOGsem);

    FILE *file = fopen(LOGPATH, "a");
    if (file == NULL) {
        fprintf(stderr, "Error opening the file.\n");
        exit(EXIT_FAILURE);
    }

    if (keyPressed == ERR) {
        fprintf(file, "[Keyboard]: no key (sent %d)\n", (int)sentKey);
    } else {
        fprintf(file, "[Keyboard]: key %c pressed\n", (char)keyPressed);
    }

    fclose(file);
    sem_post(LOGsem);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pipes>\n", argv[0]);
        return EXIT_FAILURE;
    }

    init_ncurses();
    sem_t *LOGsem = open_logging_semaphore();

    // the pipes
    int keyboard_server[2], server_keyboard[2];
    parse_pipes(argv[1], keyboard_server, server_keyboard);

    // Only `data.key` is used by the server.
    struct data data = (struct data){0};

    while (1) {
        int keyPressed = getch();

        // If no key was pressed this tick, send 0 (instead of an uninitialized byte).
        if (keyPressed != ERR) {
            data.key = (char)keyPressed;
        } else {
            data.key = 0;
        }

        write(keyboard_server[1], &data, sizeof(data));
        log_key(LOGsem, keyPressed, data.key);

        // Sleep for 50 milliseconds
        usleep(50000);
    }

    // Clean up (unreachable with current infinite loop)
    close(keyboard_server[1]);
    close(server_keyboard[0]);
    sem_close(LOGsem);
    endwin();
    return 0;
}
