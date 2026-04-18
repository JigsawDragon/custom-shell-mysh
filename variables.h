#ifndef __VARIABLES_H__
#define __VARIABLES_H__

#include <stddef.h>


typedef struct {
    char *name;
    char *value;
} EnvVar;

typedef struct {
    EnvVar *vars;
    size_t count;
    size_t capacity;
} EnvVarList;

void create_var(EnvVarList *lst);

void assign_var(EnvVarList *lst, const char *name, const char *value);

char *get_var_val(EnvVarList *lst, const char *name);

char *expand_var(EnvVarList *lst, const char *input);

void free_vars(EnvVarList *lst);

#endif