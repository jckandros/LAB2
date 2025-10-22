/* rogue.c
   On DUNGEON_SIGNAL: perform binary search for unknown float in [0, MAX_PICK_ANGLE],
   writing guesses into dungeon->rogue.pick and reacting to dungeon->trap.direction.
   On SEMAPHORE_SIGNAL: attempt to read dungeon->treasure chars one-by-one,
   appending them into dungeon->spoils; after collecting 4 chars, post both semaphores
   to release the door.
*/

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <semaphore.h>
#include <math.h>
#include <string.h>
#include "dungeon_info.h"
#include "dungeon_settings.h"
#include <stdatomic.h>

static volatile sig_atomic_t dungeon_signal_received = 0;
static volatile sig_atomic_t sem_signal_received = 0;

void handler_dungeon(int sig) { dungeon_signal_received = 1; }
void handler_semaphore(int sig) { sem_signal_received = 1; }

/* Binary search loop: dungeon modifies trap.direction and locked while watching rogue.pick */
static void rogue_do_pick(struct Dungeon *dungeon) {
    double low = 0.0, high = (double)MAX_PICK_ANGLE;
    double guess = (low + high) / 2.0;
    dungeon->rogue.pick = (float)guess;
    int ticks = 0;
    /* We will loop until trap.direction == '-' or timeout of SECONDS_TO_PICK */
    int max_iters = (SECONDS_TO_PICK * 1000000) / TIME_BETWEEN_ROGUE_TICKS + 10;
    for (int iter = 0; iter < max_iters && dungeon->running; ++iter) {
        usleep(TIME_BETWEEN_ROGUE_TICKS); /* dungeon checks these intervals */
        /* read trap.direction and adjust */
        char dir = dungeon->trap.direction;
        if (dir == '-') {
            /* success (dungeon will also set locked = false) */
            break;
        } else if (dir == 'u') {
            /* trap says pick needs to go up -> increase guess */
            low = guess;
            guess = (low + high) / 2.0;
            dungeon->rogue.pick = (float)guess;
        } else if (dir == 'd') {
            /* trap says pick needs to go down -> decrease guess */
            high = guess;
            guess = (low + high) / 2.0;
            dungeon->rogue.pick = (float)guess;
        } else {
            /* no guidance yet - tweak a bit */
            guess = (low + high) / 2.0;
            dungeon->rogue.pick = (float)guess;
        }
    }
}

/* Treasure gathering: read dungeon->treasure one char at a time into spoils */
static void rogue_do_treasure(struct Dungeon *dungeon) {
    sem_t *lever1 = sem_open(dungeon_lever_one, 0);
    sem_t *lever2 = sem_open(dungeon_lever_two, 0);
    /* The dungeon will have placed treasure chars slowly into dungeon->treasure.
       We'll read them as they appear and append to spoils. */
    int collected = 0;
    while (dungeon->running && collected < 4) {
        /* If a new char appears in treasure at index collected, copy it */
        char c = dungeon->treasure[collected];
        if (c != 0) {
            dungeon->spoils[collected] = c;
            ++collected;
        } else {
            usleep(50000);
        }
    }
    /* After collecting 4 chars, release the semaphores (post both) to let others exit) */
    if (lever1 != SEM_FAILED) sem_post(lever1);
    if (lever2 != SEM_FAILED) sem_post(lever2);
    if (lever1 != SEM_FAILED) sem_close(lever1);
    if (lever2 != SEM_FAILED) sem_close(lever2);
}

int main(void) {
    /* install handlers */
    struct sigaction sa;
    sa.sa_handler = handler_dungeon;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(DUNGEON_SIGNAL, &sa, NULL);

    struct sigaction sa2;
    sa2.sa_handler = handler_semaphore;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = SA_RESTART;
    sigaction(SEMAPHORE_SIGNAL, &sa2, NULL);

    int shm_fd = shm_open(dungeon_shm_name, O_RDWR, 0);
    if (shm_fd < 0) { perror("rogue shm_open"); return 1; }
    struct Dungeon *dungeon = mmap(NULL, sizeof(struct Dungeon),
                                   PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (dungeon == MAP_FAILED) { perror("rogue mmap"); return 1; }

    while (dungeon->running) {
        if (dungeon_signal_received) {
            dungeon_signal_received = 0;
            rogue_do_pick(dungeon);
        }
        if (sem_signal_received) {
            sem_signal_received = 0;
            rogue_do_treasure(dungeon);
        }
        usleep(20000);
    }

    munmap(dungeon, sizeof(struct Dungeon));
    close(shm_fd);
    return 0;
}
