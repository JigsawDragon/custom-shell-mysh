#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "commands.h"
#include "builtins.h"
#include "io_helpers.h"
#include "variables.h"

static Job *job_list = NULL;
static int next_job_num = 1;

void handle_pipes(char *input, EnvVarList *lst, bool is_background_task) {

    char *parts[MAX_STR_LEN];
    int total_parts = 0;
    char *part = strtok(input, "|");
    while (part != NULL) {
        parts[total_parts] = part;
        total_parts++;
        part = strtok(NULL, "|");
    }

    int pipefds[total_parts - 1][2];
    for (int i = 0; i < total_parts - 1; i++) {
        pipe(pipefds[i]);
    }

    pid_t temp_pids[total_parts];

    for (int i = 0; i < total_parts; i++) {

        char *expanded = expand_var(lst, parts[i]);
        char *tokens[MAX_STR_LEN];
        size_t token_count = tokenize_input(expanded, tokens);


        if (token_count == 0) {
            free(expanded);
            continue;
        }

        temp_pids[i] = fork();

        if (temp_pids[i] == 0) {
            if (i == 0) { // First pipe
                if (total_parts > 1) {
                    dup2(pipefds[0][1], STDOUT_FILENO);
                }
            } else if (i == total_parts - 1) { // Last pipe
                dup2(pipefds[i-1][0], STDIN_FILENO);
            } else { // Middle Pipe
                dup2(pipefds[i-1][0], STDIN_FILENO);
                dup2(pipefds[i][1], STDOUT_FILENO);
            }

            for (int j = 0; j < total_parts - 1; j++) {
                close(pipefds[j][0]);
                close(pipefds[j][1]);
            }

            bn_ptr builtin_fn = check_builtin(tokens[0]);
            if (builtin_fn != NULL) {
                builtin_fn(tokens);
            } else {
                char path[MAX_STR_LEN];

                snprintf(path, MAX_STR_LEN, "/bin/%s", tokens[0]);
                execv(path, tokens);

                snprintf(path, MAX_STR_LEN, "/usr/bin/%s", tokens[0]);
                execv(path, tokens);

                display_error("ERROR: Unknown command: ", tokens[0]);
            }
            free(expanded);
            exit(0);
        }
        free(expanded);
    }

    for (int i = 0; i < total_parts - 1; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }

    // close and wait for all children
    if (is_background_task) { // register all the pids when it's a background task
        int job_num = get_next_job_num();
        char buf[128];
        snprintf(buf, sizeof(buf), "[%d] %d\n", job_num, temp_pids[total_parts - 1]);
        display_message(buf);
        add_job(temp_pids, total_parts, job_num, parts[0]);
    } else { // otherwise just wait
        for (int i = 0; i < total_parts; i++) {
            waitpid(temp_pids[i], NULL, 0);
        }
    }

}

void add_job(pid_t *pids, int pid_count, int job_num, const char *command) {
    Job *new_job = malloc(sizeof(Job));
    new_job->pid_count = pid_count;
    new_job->done_count = 0;
    new_job->job_num = job_num;
    new_job->done = false;
    strncpy(new_job->command, command, 127);
    new_job->command[127] = '\0';

    for (int i = 0; i < pid_count; i++) {
        new_job->pids[i] = pids[i];
    }
    
    new_job->next = job_list;
    job_list = new_job;
}

Job *find_job(pid_t pid) {
    Job *curr = job_list;
    while (curr != NULL) {
        for (int i = 0; i < curr->pid_count; i++) {
            if (curr->pids[i] == pid) {
                return curr;
            }
        }
        curr = curr->next;
    }
    return NULL;
}

void remove_job(pid_t pid) {
    Job *curr = job_list;
    Job *prev = NULL;

    while (curr != NULL) {
        bool contains = false;
        for (int i = 0; i < curr->pid_count; i++) {
            if (curr->pids[i] == pid) {
                contains = true;
                break;
            }
        }

        if (contains) {
            if (prev == NULL) {
                job_list = curr->next;
            } else {
                prev->next = curr->next;
            }
            free(curr);

            if (job_list == NULL) {
                next_job_num = 1;
            }
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

void catch_children(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        Job *job = find_job(pid);
        if (job == NULL) continue;

        job->done_count++;

        if (job->done_count == job->pid_count) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[%d]+ Done %s\n", job->job_num, job->command);
            display_message(buf);
            job->done = true;
            remove_job(pid);
        }
    }
}

Job *get_job_list(void) {
    return job_list;
}

int get_next_job_num(void) {
    return next_job_num++;
}