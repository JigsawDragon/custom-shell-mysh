#ifndef __BUILTINS_H__
#define __BUILTINS_H__

#include <unistd.h>
#include <stdbool.h>

/* Type for builtin handling functions
 * Input: Array of tokens
 * Return: >=0 on success and -1 on error
 */
typedef ssize_t (*bn_ptr)(char **);

// This is for directory related stuff
char* get_current_dir(void);
void set_current_dir(const char *dir);
void starting_dir(void);

int absolute_dir(const char *path, char *buffer);

// This is for builtins
ssize_t bn_echo(char **tokens);
ssize_t bn_ls(char **tokens);
void ls_rec_helper(const char *path, bool show_hidden, char *filter, int max_depth, int current_depth);
ssize_t bn_cd(char **tokens);
ssize_t bn_cat(char **tokens);
ssize_t bn_wc(char **tokens);
ssize_t bn_kill(char **tokens);
ssize_t bn_ps(char **tokens);
ssize_t bn_start_server(char **tokens);
ssize_t bn_close_server(char **tokens);
ssize_t bn_send(char **tokens);
ssize_t bn_start_client(char **tokens);
int get_server_fd();
int *get_client_fds();
int *get_active_clients();
int *get_total_clients();


/* Return: the address of the function handling the builtin or null if cmd doesn't match a builtin
 */
bn_ptr check_builtin(const char *cmd);


/* BUILTINS and BUILTINS_FN are parallel arrays of length BUILTINS_COUNT
 */
static const char * const BUILTINS[] = {"echo", "ls", "cd", "cat", "wc", "kill", "ps", "start-server", "close-server", "send", "start-client"};
static const bn_ptr BUILTINS_FN[] = {bn_echo, bn_ls, bn_cd, bn_cat, bn_wc, bn_kill, bn_ps, bn_start_server, bn_close_server, bn_send, bn_start_client, NULL};    // Extra null element for 'non-builtin'
static const ssize_t BUILTINS_COUNT = sizeof(BUILTINS) / sizeof(char *);

#endif
