#ifndef SEVENTHOS_CMD_THREAD_H
#define SEVENTHOS_CMD_THREAD_H

void cmd_timer(void);
void cmd_delay(const char *args);
void cmd_ps(void);
void cmd_threadlog(void);
void cmd_spawn(const char *args);
void cmd_kill(const char *args);
void cmd_prio(const char *args);
void cmd_sched(const char *args);
void cmd_reap(void);

#endif