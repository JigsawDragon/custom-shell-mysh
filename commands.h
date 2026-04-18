#ifndef __COMMANDS_H__
#define __COMMANDS_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>

#include "variables.h"

void handle_pipes(char *input, EnvVarList *lst, bool is_background_task);

typedef struct Job {
    pid_t pids[128];
    int   pid_count;
    int   done_count;
    int   job_num;
    char  command[128];
    bool  done;
    struct Job *next;
} Job;

void add_job(pid_t *pids, int pid_count, int job_num, const char *command);
void remove_job(pid_t pid);
Job  *find_job(pid_t pid);
Job *get_job_list(void);
void catch_children(void);
int  get_next_job_num(void);

#endif