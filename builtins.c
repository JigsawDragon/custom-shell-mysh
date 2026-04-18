#define _DEFAULT_SOURCE // This was suggested online to make DT_DIR work
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <pwd.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "builtins.h"
#include "io_helpers.h"
#include "commands.h"

static char current_dir[MAX_STR_LEN];
static int server_fd = -1;
static int client_fds[64];
static int total_clients = 0;
static int active_clients = 0;

// ====== Command execution =====

/* Return: pointer of a function to handle builtin or -1 if cmd doesn't match a builtin
 */
bn_ptr check_builtin(const char *cmd) {
    ssize_t cmd_num = 0;
    while (cmd_num < BUILTINS_COUNT &&
           strncmp(BUILTINS[cmd_num], cmd, MAX_STR_LEN) != 0) {
        cmd_num += 1;
    }
    return BUILTINS_FN[cmd_num];
}

// ====== Directory functions =====

/*Gets current directory.*/
char* get_current_dir(void) {
    return current_dir;
}

/*Changes to a new current directory.*/
void set_current_dir(const char *dir) {
    strncpy(current_dir, dir, MAX_STR_LEN - 1);
    current_dir[MAX_STR_LEN - 1] = '\0';
}

/*Creates a starting directory or root. */
void starting_dir(void) {
    if (getcwd(current_dir, MAX_STR_LEN) == NULL) {
        strcpy(current_dir, "/");
    }
}

/* Turns a path into an absolute path.
 * Handles an arbitrary amount of dots.
 * 
*/
int absolute_dir(const char *path, char *buffer) {
    char original_dir[MAX_STR_LEN];
    char curr_path[MAX_STR_LEN];
    
    if (path == NULL || path[0] == '\0') {
        strcpy(buffer, get_current_dir());
        return 0;
    }
    
    // dot logic
    if (path[0] == '.') {
        int dots = 0;
        while (path[dots] == '.') dots++;
        
        if (dots > 2) {
            curr_path[0] = '\0';
            for (int i = 0; i < dots - 1; i++) {
                strcat(curr_path, "..");
                if (i < dots - 2) {
                    strcat(curr_path, "/");
                }
            }
            if (path[dots] != '\0') {
                strcat(curr_path, "/");
                strcat(curr_path, path + dots);
            }
        } else {
            strcpy(curr_path, path);
        }
    } else {
        strcpy(curr_path, path);
    }
    
    getcwd(original_dir, MAX_STR_LEN);

    if (chdir(curr_path) == -1) {
    return -1;
}
    getcwd(buffer, MAX_STR_LEN);
    chdir(original_dir);
    return 0;
}


// ===== Builtins =====

/* Prereq: tokens is a NULL terminated sequence of strings.
 * Return 0 on success and -1 on error ... but there are no errors on echo. 
 */
ssize_t bn_echo(char **tokens) {
    ssize_t index = 1;

    if (tokens[index] != NULL) {
        display_message(tokens[index]);
        index += 1;
    }
    while (tokens[index] != NULL) {
        display_message(" ");
        display_message(tokens[index]);
        index += 1;
    }
    display_message("\n");

    return 0;
}

void ls_rec_helper(const char *path, bool show_hidden, char *filter, int max_depth, int current_depth) {
    //nearly the same as the main ls func, only with recursive step
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return;
    }

    struct dirent *entry;
    entry = readdir(dir);
    while (entry != NULL) {
        char *name = entry->d_name;

        if (name[0] == '.' && strncmp(name, ".", MAX_STR_LEN) != 0
                           && strncmp(name, "..", MAX_STR_LEN) != 0) {
            if (!show_hidden) {
                entry = readdir(dir);
                continue;
            }
        }

        if (filter == NULL || strstr(name, filter) != NULL) {
            display_message(name);
            display_message("\n");
        }

        if (strncmp(name, ".", MAX_STR_LEN) != 0
                && strncmp(name, "..", MAX_STR_LEN) != 0
                && (max_depth == -1 || current_depth < max_depth)) {
            int path_len = strlen(path) + strlen(name) + 2;
            char *child_path = malloc(path_len);
            if (child_path == NULL) {
                return;
            }
            snprintf(child_path, path_len, "%s/%s", path, name);
            DIR *child_dir = opendir(child_path);
            if (child_dir != NULL) {
                closedir(child_dir);
                ls_rec_helper(child_path, show_hidden, filter, max_depth, current_depth + 1);
            }
            free(child_path);
        }
        entry = readdir(dir);
    }
    closedir(dir);
}

ssize_t bn_ls(char **tokens) {

    // vars for all flags
    char* path = NULL;
    bool show_hidden = false; // for --a flag
    char* filter = NULL; // for --f flag
    bool is_recursive = false; // for --rec flag
    int max_depth = -1; // for --d flag

    int i = 1;
    while (tokens[i] != NULL) {
        if(strncmp(tokens[i], "--a", MAX_STR_LEN) == 0) {
            show_hidden = true;
        } else if (strncmp(tokens[i], "--f", MAX_STR_LEN) == 0) {
            if (tokens[i + 1] != NULL) {
                filter = tokens[i + 1];
                i++;
            }
        } else if (strncmp(tokens[i], "--rec", MAX_STR_LEN) == 0) {
            is_recursive = true;
        } else if (strncmp(tokens[i], "--d", MAX_STR_LEN) == 0) {
            if (tokens[i + 1] != NULL) {
                max_depth = (int)strtol(tokens[i + 1], NULL, 10);
                i++;
            }
        } else {
            path = tokens[i];
        }
        i++;
    }

    // checks if --d appears with --rec
    if (max_depth != -1 && !is_recursive) {
        display_error("ERROR: Builtin failed: ls\n", "");
        return -1;
    }

    char buffer[MAX_STR_LEN];
    if (absolute_dir(path, buffer) == -1) {
        display_error("ERROR: Invalid path\n", "");
        return -1;
    }

    // calls recursion and checks flag conditions
    if (is_recursive) {
        ls_rec_helper(buffer, show_hidden, filter, max_depth, 0);
    } else {
        DIR *dir = opendir(buffer);
        if (dir == NULL) {
            display_error("ERROR: Invalid path\n", "");
            return -1;
        }

        struct dirent *entry;
        entry = readdir(dir);
        while(entry != NULL) {
            char *name = entry->d_name;

            if(name[0] == '.' && strncmp(name, ".", MAX_STR_LEN) != 0 
                    && strncmp(name, "..", MAX_STR_LEN) != 0) {
                if (!show_hidden) {
                    entry = readdir(dir);
                    continue;
                }
            }

            if (filter != NULL && strstr(name, filter) == NULL) {
                entry = readdir(dir);
                continue;
            }

            display_message(name);
            display_message("\n");
            entry = readdir(dir);
        }
        closedir(dir);
    }
    return 0;
}

ssize_t bn_cd(char **tokens) {
    if (tokens[2] != NULL) {
        display_error("ERROR: Too many arguments: cd takes a single path\n", "");
        return -1;
    }
    
    const char *target_path = tokens[1];
    
    // go to home dir when blank arguement
    if (target_path == NULL) {
        struct passwd *pw = getpwuid(getuid());
        if (pw == NULL || pw->pw_dir == NULL) {
            display_error("ERROR: Invalid path\n", "");
            return -1;
        }
        target_path = pw->pw_dir;
    }

    char buffer[MAX_STR_LEN];
    if (absolute_dir(target_path, buffer) == -1) {
        display_error("ERROR: Invalid path\n", "");
        return -1;
    }

    chdir(buffer);
    set_current_dir(buffer);

    return 0;
}


ssize_t bn_cat(char **tokens) {

    FILE *file = NULL;
    // Error checks
    if (tokens[1] != NULL) {
        if (tokens[2] != NULL) {
            display_error("ERROR: Too many arguments: cat takes a single file", "");
            return -1;
        }
        file = fopen(tokens[1], "r");
        if (file == NULL) {
            display_error("ERROR: Cannot open file", "");
            return -1;
        }
    } else if (!isatty(STDIN_FILENO)) {
        file = stdin;
    } else {
        display_error("ERROR: No input source provided", "");
        return -1;
    }
        

    // Loop to keep reading chunks of file bytes
    char temp[128];
    size_t bytes_read;
    bytes_read = fread(temp, 1, sizeof(temp), file);
    while (bytes_read > 0) {
        write(STDOUT_FILENO, temp, bytes_read);
        bytes_read = fread(temp, 1, sizeof(temp), file);
    }
    
    if (file != stdin) fclose(file);
    return 0;
}


ssize_t bn_wc(char **tokens) {
    FILE *file = NULL;
    // Error checks
    if (tokens[1] != NULL) {
        if (tokens[2] != NULL) {
            display_error("ERROR: Too many arguments: cat takes a single file", "");
            return -1;
        }
        file = fopen(tokens[1], "r");
        if (file == NULL) {
            display_error("ERROR: Cannot open file", "");
            return -1;
        }
    } else if (!isatty(STDIN_FILENO)) {
        file = stdin;
    } else {
        display_error("ERROR: No input source provided", "");
        return -1;
    }
    
    int word_count = 0, char_count = 0, newline_count = 0;
    bool curr_word = false;

    char temp[128];
    size_t bytes_read = fread(temp, 1, sizeof(temp), file);
    while (bytes_read > 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            char c = temp[i];
            char_count++;
            
            if (c == '\n') {
                newline_count++;
                curr_word = false;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                curr_word = false;
            } else if (!curr_word) {
                word_count++;
                curr_word = true;
            }
        }
        bytes_read = fread(temp, 1, sizeof(temp), file);
    }
    if (file != stdin) fclose(file);

    char output[256];
    sprintf(output, "word count %d\ncharacter count %d\nnewline count %d\n", word_count, char_count, newline_count);
    display_message(output);
    
    return 0;
}

ssize_t bn_kill(char **tokens) {// Error checking
    if (tokens[1] == NULL) {
        display_error("ERROR: No input source provided\n", "");
        return -1;
    }
    
    pid_t pid = (pid_t)strtol(tokens[1], NULL, 10);

    int signum = SIGTERM;
    if (tokens[2] != NULL) {
        signum = (int)strtol(tokens[2], NULL, 10);
        if (signum <= 0 || signum >= NSIG) {
            display_error("ERROR: Invalid signal specified", "");
            return -1;
        }
    }

    if (kill(pid, signum) == -1) {
        display_error("ERROR: The process does not exist", "");
        return -1;
    }

    return 0;
}

ssize_t bn_ps(char **tokens) {
    (void)tokens;
    Job *curr = get_job_list();
    while (curr != NULL) {
        if (!(curr->done)) {
            for (int i = 0; i < curr->pid_count; i++) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s %d\n", curr->command, curr->pids[i]);
                display_message(buf);
            }
        }
        curr = curr->next;
    }
    return 0;
}

ssize_t bn_start_server(char **tokens) {
    if (tokens[1] == NULL) {
        display_error("ERROR: No port provided", "");
        return -1;
    }

    int port = (int)strtol(tokens[1], NULL, 10);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        display_error("ERROR: Could not bind to port", "");
        close(server_fd);
        server_fd = -1;
        return -1; 
    }

    listen(server_fd, 10);
    return 0;
}

ssize_t bn_close_server(char **tokens) {
    (void)tokens;
    if (server_fd == -1) {
        display_error("ERROR: No server running", "");
        return -1;
    }

    for (int i = 0; i < active_clients; i++) {
        close(client_fds[i]);
    }
    active_clients = 0;
    total_clients = 0;

    close(server_fd);
    server_fd = -1;
    return 0;
}

ssize_t bn_send(char **tokens) {
    if (tokens[1] == NULL) {
        display_error("ERROR: No port provided", "");
        return -1;
    }

    if (tokens[2] == NULL) {
        display_error("ERROR: No hostname provided", "");
        return -1;
    }

    int port = (int)strtol(tokens[1], NULL, 10);
    char* hostname = tokens[2]; 
    char message[MAX_STR_LEN] = "";

    int i = 3;
    while(tokens[i] != NULL) {
        if (i > 3) strcat(message, " ");
        strcat(message, tokens[i]);
        i++;
    }

    if (strlen(message) > 127) {
        display_error("ERROR: Message too long", "");
        return -1;
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, hostname, &addr.sin_addr);

    if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        display_error("ERROR: Could not connect to server", "");
        close(s);
        return -1;
    }

    write(s, message, strlen(message));
    write(s, "\n", 1);
    close(s);

    return 0;
}

ssize_t bn_start_client(char **tokens) {
    if (tokens[1] == NULL) {
        display_error("ERROR: No port provided", "");
        return -1;
    }

    if (tokens[2] == NULL) {
        display_error("ERROR: No hostname provided", "");
        return -1;
    }

    int port = (int)strtol(tokens[1], NULL, 10);
    char* hostname = tokens[2]; 

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1) {
        display_error("ERROR: Could not create socket", "");
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, hostname, &addr.sin_addr);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        display_error("ERROR: Could not connect to server", "");
        close(s);
        return -1;
    }

    char id_buf[MAX_STR_LEN];
    int id_len = read(s, id_buf, MAX_STR_LEN - 1);
    if (id_len > 0) {
        id_buf[id_len] = '\0';
    }

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(s, &read_fds);
        int max_fd = s;

        int sel = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (sel == -1) {
            break;
        }

        if (FD_ISSET(s, &read_fds)) {
            char msg_buf[MAX_STR_LEN];
            int n = read(s, msg_buf, MAX_STR_LEN - 1);
            if (n <= 0) {
                break;
            }
            msg_buf[n] = '\0';
            display_message(msg_buf);
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char input[MAX_STR_LEN];
            int n = read(STDIN_FILENO, input, MAX_STR_LEN - 1);
            if (n <= 0) {
                break;
            }
            input[n] = '\0';
            if (input[0] == '\0') {
                break;
            }
            if (strncmp(input, "\\connected", 10) == 0) {
                char connected_msg[MAX_STR_LEN];
                snprintf(connected_msg, MAX_STR_LEN, "%d\n", active_clients);
                display_message(connected_msg);
                continue;
            }

            char full_msg[MAX_STR_LEN * 2];
            snprintf(full_msg, MAX_STR_LEN * 2, "%s%s\n", id_buf, input);
            write(s, full_msg, strlen(full_msg));
        }
    }
    close(s);
    return 0;
}

int get_server_fd() { 
    return server_fd; 
}
int *get_client_fds() { 
    return client_fds;
}
int* get_active_clients() { 
    return &active_clients;
}
int* get_total_clients() {
    return &total_clients;
}