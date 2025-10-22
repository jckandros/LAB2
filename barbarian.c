/* barbarian.c
   Responds to DUNGEON_SIGNAL by copying enemy.health -> barbarian.attack,
   then waits SECONDS_TO_ATTACK and leaves the value (Dungeon checks for success).
   Responds to SEMAPHORE_SIGNAL by waiting on one of the levers to "hold" the door.
*/

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <semaphore.h>
#include "dungeon_info.h"
#include "dungeon_settings.h"
#include <stdatomic.h>

static volatile sig_atomic_t dungeon_signal_received = 0;
static volatile sig_atomic_t sem_signal_received = 0;

void handler_dungeon(int sig) { dungeon_signal_received = 1; }
void handler_semaphore(int sig) { sem_signal_received = 1; }

int main(void) {
    /* Setup signal handlers */
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

    /* open shared memory and map */
    int shm_fd = shm_open(dungeon_shm_name, O_RDWR, 0);
    if (shm_fd < 0) { perror("barbarian shm_open"); return 1; }
    struct Dungeon *dungeon = mmap(NULL, sizeof(struct Dungeon),
                                   PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (dungeon == MAP_FAILED) { perror("barbarian mmap"); return 1; }

    /* Open semaphore (do not create) */
    sem_t *lever1 = sem_open(dungeon_lever_one, 0);
    if (lever1 == SEM_FAILED) { perror("barbarian sem_open lever1"); /* but continue without semaphores */ }

    /* main loop */
    while (dungeon->running) {
        if (dungeon_signal_received) {
            dungeon_signal_received = 0;
            /* copy enemy health to attack */
            int h = dungeon->enemy.health;
            dungeon->barbarian.attack = h;
            /* simulate attack time */
            sleep(SECONDS_TO_ATTACK);
            /* leaving the value; dungeon checks for correctness */
        }
        if (sem_signal_received) {
            sem_signal_received = 0;
            /* Barbarian will try to hold one lever (sem_wait). If sem_open failed, skip safely. */
            if (lever1 != SEM_FAILED) {
                sem_wait(lever1); /* hold lever until rogue posts it later */
                /* After sem_wait, we hold it. We'll release when rogue finishes, but we can't block here forever.
                   To implement "hold until rogue done" we keep the semaphore held until dungeon->running becomes false
                   OR until we see dungeon->spoils filled (4 chars) — simple heuristic: wait until spoils full. */
                while (dungeon->spoils[3] == 0 && dungeon->running) {
                    usleep(20000);
                }
                sem_post(lever1); /* release */
            }
        }
        usleep(20000);
    }

    /* cleanup */
    if (lever1 != SEM_FAILED) sem_close(lever1);
    munmap(dungeon, sizeof(struct Dungeon));
    close(shm_fd);
    return 0;
}
