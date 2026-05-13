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

struct data data;
int targetReached = 0;


FILE* file;
sem_t* LOGsem; 
char msg[100];

void logit(char *msg){
      sem_wait(LOGsem);

        file = fopen( LOGPATH, "a");
        // Check if the file was opened successfully
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

int main(int argc, char *argv[])
{
      // Create or open a semaphore for logging
    LOGsem = sem_open(LOGSEMPATH, O_RDWR, 0666); 
    if (LOGsem == SEM_FAILED) {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

     // PIPES
    int  obstacles_server[2], server_obstacles[2];
    sscanf(argv[0], "%d %d|%d %d",  &obstacles_server[0], &obstacles_server[1], &server_obstacles[0], &server_obstacles[1]);
    close(obstacles_server[0]); //Close unnecessary pipes
    close(server_obstacles[1]);

    sprintf(msg,"[Obstacles]: ALIVE");
    logit(msg);
    
          
    read(server_obstacles[0], &data, sizeof(data));


    while(1) {
       // Create obstacles
        for (int i = 0; i < NUM_OBSTACLES * 2; i += 2) {
            do {
                data.obstacles[i] = rand() % (int)data.max[0];
            } while (fabs(data.obstacles[i] - data.targets[i]) < THRESH_TARGET || fabs(data.obstacles[i] - data.drone_pos[0]) < THRESH_TARGET);

            do {
                data.obstacles[i + 1] = rand() % (int)data.max[1];
            } while (fabs(data.obstacles[i + 1] - data.targets[i + 1]) < THRESH_TARGET || fabs(data.obstacles[i + 1] - data.drone_pos[1]) < THRESH_TARGET);

            sprintf(msg, "[Obstacles]: x[%d]: %f, y[%d]: %f", i/2, data.obstacles[i], i/2, data.obstacles[i + 1]);
            logit(msg);
        }
        write(obstacles_server[1], &data, sizeof(data));
        sleep(10);
        read(server_obstacles[0], &data, sizeof(data)); //update dronepos
    }



    close(obstacles_server[1]); //Close unnecessary pipes
    close(server_obstacles[0]);
}