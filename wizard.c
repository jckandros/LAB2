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
 
