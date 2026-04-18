#include <stdlib.h>
#include <string.h>
#include "variables.h"
#include <stdio.h>


void create_var(EnvVarList *lst) {
    lst->capacity = 2;
    lst->count = 0;
    lst->vars = malloc(lst->capacity * sizeof(EnvVar));
}

void assign_var(EnvVarList *lst, const char *name, const char *value) {
    for (size_t i = 0; i < lst->count; i++) {
        if (strcmp(lst -> vars[i].name, name) == 0) {
            free(lst->vars[i].value);
            lst->vars[i].value = malloc(strlen(value) + 1);
            strcpy(lst->vars[i].value, value);
            return;
        }
    }

    if (lst->count == lst->capacity) {
        size_t new_capacity = lst->capacity * 2;
        EnvVar *new_list = malloc(new_capacity * sizeof(EnvVar));
        
        for (size_t i = 0; i < lst->count; i++) {
            new_list[i] = lst->vars[i];
        }
        
        free(lst->vars);
        
        lst->vars = new_list;
        lst->capacity = new_capacity;
    }

    lst->vars[lst->count].name = malloc(strlen(name) + 1);
    strcpy(lst->vars[lst->count].name, name); //LHS
    
    lst->vars[lst->count].value = malloc(strlen(value) + 1);
    strcpy(lst->vars[lst->count].value, value); //RHS
    
    lst->count++;
    
    return;
}

char *get_var_val(EnvVarList *lst, const char *name) {
    for (size_t i = 0; i < lst->count; i++) {
        if (strcmp(lst->vars[i].name, name) == 0) {
            return lst->vars[i].value;
        }
    }
    return NULL;
}

char *expand_var(EnvVarList *lst, const char *input) {
    char *result = malloc(2048);
    int result_index = 0;
    int input_index = 0;
    int curr_token_len = 0;
    
    while (input[input_index] != '\0') {
        if (input[input_index] == ' ') {
            result[result_index] = ' ';
            result_index++;
            input_index++;
            curr_token_len = 0;
        } 
        else if (input[input_index] == '$') {
            input_index++;
            
            char var_name[129];
            int var_name_index = 0;
            
            while (input[input_index] != '\0' && input[input_index] != ' ' && input[input_index] != '$' && input[input_index] != '\n') {
                var_name[var_name_index] = input[input_index];
                var_name_index++;
                input_index++;
            }
            var_name[var_name_index] = '\0';
            
            const char *var_value = get_var_val(lst, var_name);
            if (var_value == NULL) {
                var_value = "";
            }
            
            int value_index = 0;
            while (var_value[value_index] != '\0' && curr_token_len < 128) {
                result[result_index] = var_value[value_index];
                result_index++;
                value_index++;
                curr_token_len++;
            }
        } 
        else {
            if (curr_token_len < 128) {
                result[result_index] = input[input_index];
                result_index++;
                curr_token_len++;
            }
            input_index++;
        }
    }
    
    result[result_index] = '\0';
    return result;
}

void free_vars(EnvVarList *lst) {
    for (size_t i = 0; i < lst->count; i++) {
        free(lst->vars[i].name);
        free(lst->vars[i].value);
    }
    free(lst->vars);
}