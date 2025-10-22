/* wizard.c
   On DUNGEON_SIGNAL: read barrier.spell (Caesar-style) and decode
   using first character as key (numeric), store decoded string in wizard.spell.
   On SEMAPHORE_SIGNAL: wait on a semaphore to hold a lever until rogue completes.
*/

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <semaphore.h>
#include <ctype.h>
#include <string.h>
#include "dungeon_info.h"
#include "dungeon_settings.h"
#include <stdatomic.h>

static volatile sig_atomic_t dungeon_signal_received = 0;
static volatile sig_atomic_t sem_signal_received = 0;

void handler_dungeon(int sig) { dungeon_signal_received = 1; }
void handler_semaphore(int sig) { sem_signal_received = 1; }

/* decode Caesar cipher: first character is numeric key (use its ASCII code) */
static void decode_caesar(const char *in, char *out) {
    if (!in || !out) return;
    size_t len = strlen(in);
    if (len == 0) { out[0] = '\0'; return; }
    int key = (unsigned char)in[0]; /* use numeric value */
    /* reduce key to 0..25 */
    key = key % 26;
    /* decode starting from index 1 */
    size_t oi = 0;
    for (size_t i = 1; i < len && oi < SPELL_BUFFER_SIZE - 1; ++i) {
        char c = in[i];
        if (isupper((unsigned char)c)) {
            char base = 'A';
            int val = c - base;
            int decoded = (val - key) % 26;
            if (decoded < 0) decoded += 26;
            out[oi++] = base + decoded;
        } else if (islower((unsigned char)c)) {
            char base = 'a';
            int val = c - base;
            int decoded = (val - key) % 26;
            if (decoded < 0) decoded += 26;
            out[oi++] = base + decoded;
        } else {
            /* keep punctuation/space as-is */
            out[oi++] = c;
        }
    }
    out[oi] = '\0';
}

int main(void) {
    /* install signal handlers */
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

    /* open shared memory */
    int shm_fd = shm_open(dungeon_shm_name, O_RDWR, 0);
    if (shm_fd < 0) { perror("wizard shm_open"); return 1; }
    struct Dungeon *dungeon = mmap(NULL, sizeof(struct Dungeon),
                                   PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (dungeon == MAP_FAILED) { perror("wizard mmap"); return 1; }

    sem_t *lever2 = sem_open(dungeon_lever_two, 0);
    if (lever2 == SEM_FAILED) { /* continue - print a warning */ }

    while (dungeon->running) {
        if (dungeon_signal_received) {
            dungeon_signal_received = 0;
            /* copy barrier.spell into local buffer and decode */
            char in[SPELL_BUFFER_SIZE + 1];
            strncpy(in, dungeon->barrier.spell, SPELL_BUFFER_SIZE);
            in[SPELL_BUFFER_SIZE] = '\0';
            /* decode into wizard.spell */
            decode_caesar(in, dungeon->wizard.spell);
            /* simulate thinking time */
            sleep(SECONDS_TO_GUESS_BARRIER);
            /* dungeon will inspect wizard->spell to determine success */
        }
        if (sem_signal_received) {
            sem_signal_received = 0;
            if (lever2 != SEM_FAILED) {
                sem_wait(lever2);
                /* Hold until spoils filled or dungeon ends */
                while (dungeon->spoils[3] == 0 && dungeon->running) {
                    usleep(20000);
                }
                sem_post(lever2);
            }
        }
        usleep(20000);
    }

    if (lever2 != SEM_FAILED) sem_close(lever2);
    munmap(dungeon, sizeof(struct Dungeon));
    close(shm_fd);
    return 0;
}
