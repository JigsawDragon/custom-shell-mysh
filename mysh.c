#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>


#include "builtins.h"
#include "io_helpers.h"
#include "variables.h"
#include "commands.h"
#include <sys/select.h>

pid_t curr_pid = -1;

void handler(int sig) {
    (void)sig; // For unused warning bypass
    if (curr_pid > 0) {
        kill(curr_pid, SIGINT);
    }
}

void child_handler(int sig) {
    (void) sig;
    catch_children();
}

// You can remove __attribute__((unused)) once argc and argv are used.
int main(__attribute__((unused)) int argc, 
         __attribute__((unused)) char* argv[]) {
    char *prompt = "mysh$ ";

    signal(SIGINT, handler);
    signal(SIGCHLD, child_handler);

    starting_dir();

    EnvVarList var_lst;
    create_var(&var_lst);

    char input_buf[MAX_STR_LEN + 1];
    input_buf[MAX_STR_LEN] = '\0';
    char *token_arr[MAX_STR_LEN] = {NULL};

    while (1) {
        // Prompt and input tokenization
        catch_children();
        display_message(prompt);

            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(STDIN_FILENO, &read_fds);
            int max_fd = STDIN_FILENO;

            int server_fd = get_server_fd();
            if (server_fd != -1) {
                FD_SET(server_fd, &read_fds);
                if (server_fd > max_fd) {
                    max_fd = server_fd;
                    
                }
            }

            int *client_fds = get_client_fds();
            int *active_clients = get_active_clients();
            int *total_clients = get_total_clients();
            for (int i = 0; i < *active_clients; i++) {
                FD_SET(client_fds[i], &read_fds);
                if (client_fds[i] > max_fd) {
                    max_fd = client_fds[i];
                }
            }

            int sel = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
            if (sel == -1) {
                continue;
            }

            if (server_fd != -1 && FD_ISSET(server_fd, &read_fds)) {
                int new_client = accept(server_fd, NULL, NULL);
                if (new_client != -1) {
                    client_fds[*active_clients] = new_client;
                    (*active_clients)++;
                    (*total_clients)++;
                    char id_buf[MAX_STR_LEN];
                    snprintf(id_buf, MAX_STR_LEN, "client%d:", *total_clients);
                    write(new_client, id_buf, strlen(id_buf));
                }
            }

            for (int i = 0; i < *active_clients; i++) {
                if (FD_ISSET(client_fds[i], &read_fds)) {
                    char msg_buf[MAX_STR_LEN];
                    int n = read(client_fds[i], msg_buf, MAX_STR_LEN - 1);
                    if (n <= 0) {
                        close(client_fds[i]);
                        client_fds[i] = client_fds[*active_clients - 1];
                        (*active_clients)--;
                    } else {
                        msg_buf[n] = '\0';
                        display_message(msg_buf);
                        for (int j = 0; j < *active_clients; j++) {
                            if (client_fds[j] != client_fds[i]) {
                                write(client_fds[j], msg_buf, strlen(msg_buf));
                            }
                        }
                    }
                }
            }
            if (!FD_ISSET(STDIN_FILENO, &read_fds)) {
                continue;
            }

        int ret = get_input(input_buf);
        if (ret == -1) {
            continue;
        }
        
        if (ret == 0) {
            break;
        }

        char *line_start = input_buf;
        char *newline_pos;
        
        while (line_start != NULL && *line_start != '\0') {
            newline_pos = strchr(line_start, '\n');
            
            if (newline_pos != NULL) {
                *newline_pos = '\0';
            }
            
            if (*line_start == '\0') {
                if (newline_pos != NULL) {
                    line_start = newline_pos + 1;
                    continue;
                } else {
                    break;
                }
            }
            
            int line_len = strlen(line_start);
            bool is_background_task = false;
            if (line_len > 0 && line_start[line_len - 1] == '&') {
                is_background_task = true;
                line_start[line_len - 1] = '\0';
                line_len--;
                while (line_len > 0 && line_start[line_len - 1] == ' ') {
                    line_start[line_len - 1] = '\0';
                    line_len--;
                }
            }

            if (strchr(line_start, '|') != NULL) {
                handle_pipes(line_start, &var_lst, is_background_task);
            } else {
                //Assignment checking
                bool assign = false;
                int halt_position = -1;
    
                for (int i = 0; line_start[i] != '\0' && line_start[i] != ' '; i++) {
                    if (line_start[i] == '=') {
                        assign = true;
                        halt_position = i;
                        break;
                    }
                }
    
                if (assign) {
                    bool valid = true;
                    for (int i = 0; i < halt_position; i++) {
                        if (line_start[i] == '$') {
                            valid = false;
                            break;
                        }
                    }
                    if (valid) {
                        char name[MAX_STR_LEN];
                        char value[MAX_STR_LEN];
    
                        int count = 0;
                        for (int i = 0; i < halt_position; i++) {
                            name[count] = line_start[i];
                            count++;
                        }
                        name[count] = '\0';
    
                        count = 0;
                        int i = halt_position + 1;
                        while(line_start[i] != '\0' && line_start[i] != ' ' && line_start[i] != '\n') {
                            value[count] = line_start[i];
                            count++;
                            i++;
                        }
                        value[count] = '\0';
    
                        char *expanded_value = expand_var(&var_lst, value);
                        assign_var(&var_lst, name, expanded_value);
                        free(expanded_value);
                    }
                } else {
                    char *expanded = expand_var(&var_lst, line_start);
                    size_t token_count = tokenize_input(expanded, token_arr);

                    if (token_count > 0 && (strncmp("exit", token_arr[0], 4) == 0 && strlen(token_arr[0]) == 4)) {
                        free(expanded);
                        free_vars(&var_lst);
                        return 0;
                    }

                    if (token_count >= 1) {
                        bn_ptr builtin_fn = check_builtin(token_arr[0]);
                        if (builtin_fn != NULL) {
                            if (is_background_task) {
                                pid_t r = fork();
                                if (r == 0) {
                                    close(STDIN_FILENO);
                                    builtin_fn(token_arr);
                                    exit(0);
                                } else if (r > 0) {
                                    int job_num = get_next_job_num();
                                    char buf[128];
                                    snprintf(buf, sizeof(buf), "[%d] %d\n", job_num, r);
                                    display_message(buf);
                                    add_job(&r, 1, job_num, token_arr[0]);
                                }
                            } else {
                                if (strcmp(token_arr[0], "cd") == 0 ||
                                    strcmp(token_arr[0], "start-server") == 0 ||
                                    strcmp(token_arr[0], "close-server") == 0 ||
                                    strcmp(token_arr[0], "start-client") == 0) {
                                    builtin_fn(token_arr);
                                } else {
                                    pid_t r = fork();
                                    if (r == 0) {
                                        builtin_fn(token_arr);
                                        exit(0);
                                    } else if (r > 0) {
                                        curr_pid = r;
                                        waitpid(r, NULL, 0);
                                        curr_pid = -1;
                                    }
                                }
                            }
                        } else {
                            pid_t r = fork();
                            if (r == 0) {
                                if (is_background_task) {
                                    close(STDIN_FILENO);
                                }
                                char path[MAX_STR_LEN];
                                snprintf(path, MAX_STR_LEN, "/bin/%s", token_arr[0]);
                                execv(path, token_arr);

                                snprintf(path, MAX_STR_LEN, "/usr/bin/%s", token_arr[0]);
                                execv(path, token_arr);
                                
                                display_error("ERROR: Unknown command: ", token_arr[0]);
                                exit(1);
                            } else if (r > 0) {
                                if (is_background_task) {
                                    int job_num = get_next_job_num();
                                    char buf[64];
                                    snprintf(buf, sizeof(buf), "[%d] %d\n", job_num, r);
                                    display_message(buf);
                                    add_job(&r, 1, job_num, token_arr[0]);
                                } else {
                                    curr_pid = r;
                                    waitpid(r, NULL, 0);
                                    curr_pid = -1;
                                }
                            }
                        }
                    }
                    free(expanded);
                }
            }

            if (newline_pos != NULL) {
                line_start = newline_pos + 1;
                if (*line_start != '\0') {
                    display_message(prompt);
                }
            } else {
                break;
            }
        }
    }

    free_vars(&var_lst);
    return 0;
}
