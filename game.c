/* game.c
   Launcher: sets up shared memory, semaphores, forks/execs three player processes,
   calls RunDungeon (in dungeon.o), and waits for completion.
*/

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <string.h>
#include "dungeon_info.h"
#include "dungeon_settings.h"

/* helper to print errors and exit */
static void perror_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    /* Create shared memory (owner/creator) */
    int shm_fd = shm_open(dungeon_shm_name, O_CREAT | O_RDWR, 0600);
    if (shm_fd < 0) perror_exit("shm_open");

    size_t size = sizeof(struct Dungeon);
    if (ftruncate(shm_fd, size) < 0) perror_exit("ftruncate");

    struct Dungeon *dungeon = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (dungeon == MAP_FAILED) perror_exit("mmap");

    /* Initialize dungeon fields */
    dungeon->running = true;
    dungeon->dungeonPID = getpid();
    dungeon->barbarian.attack = 0;
    dungeon->rogue.pick = 0.0f;
    memset(dungeon->wizard.spell, 0, SPELL_BUFFER_SIZE);
    memset(dungeon->barrier.spell, 0, SPELL_BUFFER_SIZE + 1);
    dungeon->enemy.health = 0;
    dungeon->trap.direction = 'x';
    dungeon->trap.locked = true;
    memset(dungeon->treasure, 0, sizeof(dungeon->treasure));
    memset(dungeon->spoils, 0, sizeof(dungeon->spoils));

    /* Create named semaphores for levers (the spec said to create before RunDungeon) */
    sem_t *lever1 = sem_open(dungeon_lever_one, O_CREAT | O_EXCL, 0600, 1);
    if (lever1 == SEM_FAILED) {
        /* If exists, try opening without O_EXCL */
        lever1 = sem_open(dungeon_lever_one, O_CREAT, 0600, 1);
        if (lever1 == SEM_FAILED) perror_exit("sem_open lever1");
    }
    sem_t *lever2 = sem_open(dungeon_lever_two, O_CREAT | O_EXCL, 0600, 1);
    if (lever2 == SEM_FAILED) {
        lever2 = sem_open(dungeon_lever_two, O_CREAT, 0600, 1);
        if (lever2 == SEM_FAILED) perror_exit("sem_open lever2");
    }

    /* Fork / exec the three processes: wizard, rogue, barbarian */
    pid_t wizard_pid = fork();
    if (wizard_pid == 0) {
        execlp("./wizard", "wizard", NULL);
        perror("exec wizard");
        _exit(1);
    }

    pid_t rogue_pid = fork();
    if (rogue_pid == 0) {
        execlp("./rogue", "rogue", NULL);
        perror("exec rogue");
        _exit(1);
    }

    pid_t barbarian_pid = fork();
    if (barbarian_pid == 0) {
        execlp("./barbarian", "barbarian", NULL);
        perror("exec barbarian");
        _exit(1);
    }

    printf("Launched wizard(%d) rogue(%d) barbarian(%d)\n",
           wizard_pid, rogue_pid, barbarian_pid);
    fflush(stdout);

    /* Call RunDungeon provided in dungeon.o */
    RunDungeon(wizard_pid, rogue_pid, barbarian_pid);

    /* When RunDungeon returns, clean up: tell children to stop, wait, unlink semaphores and shared memory */
    dungeon->running = false;

    /* Give children a moment to exit */
    sleep(1);

    /* wait for children */
    int status;
    while (wait(&status) > 0) { /* reap all */ }

    /* Close and unlink semaphores */
    sem_close(lever1);
    sem_close(lever2);
    sem_unlink(dungeon_lever_one);
    sem_unlink(dungeon_lever_two);

    /* Unmap and unlink shared memory */
    munmap(dungeon, size);
    close(shm_fd);
    shm_unlink(dungeon_shm_name);

    printf("Game end, cleaned up resources.\n");
    return 0;
}
