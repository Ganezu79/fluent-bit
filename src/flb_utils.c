/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015-2017 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <msgpack.h>

#include <monkey/mk_core.h>
#include <fluent-bit/flb_macros.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_error.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_output.h>
#include <fluent-bit/flb_utils.h>

#if defined(_WIN32) || defined(_WIN64)
#define PATH_MAX 1024
#endif

#define get_key(a, b, c)   mk_rconf_section_get_key(a, b, c)
#define n_get_key(a, b, c) (intptr_t) get_key(a, b, c)
#define s_get_key(a, b, c) (char *) get_key(a, b, c)

void flb_utils_error(int err)
{
    char *msg = NULL;

    switch (err) {
    case FLB_ERR_CFG_FILE:
        msg = "Could not open configuration file";
        break;
    case FLB_ERR_CFG_FILE_FORMAT:
        msg = "Configuration file contains format errors";
        break;
    case FLB_ERR_CFG_FILE_STOP:
        msg = "Configuration file contain errors";
        break;
    case FLB_ERR_CFG_FLUSH:
        msg = "Invalid flush value";
        break;
    case FLB_ERR_CFG_FLUSH_CREATE:
        msg = "Could not create timer for flushing";
        break;
    case FLB_ERR_CFG_FLUSH_REGISTER:
        msg = "Could not register timer for flushing";
        break;
    case FLB_ERR_INPUT_INVALID:
        msg = "Invalid input type";
        break;
    case FLB_ERR_INPUT_UNDEF:
        msg = "No Input(s) have been defined";
        break;
    case FLB_ERR_INPUT_UNSUP:
        msg = "Unsupported Input";
        break;
    case FLB_ERR_OUTPUT_UNDEF:
        msg = "You must specify an output target";
        break;
    case FLB_ERR_OUTPUT_INVALID:
        msg = "Invalid output target";
        break;
    case FLB_ERR_OUTPUT_UNIQ:
        msg = "Just one output type is supported";
        break;
    case FLB_ERR_FILTER_INVALID:
        msg = "Invalid filter plugin";
        break;
    case FLB_ERR_JSON_INVAL:
        msg = "Invalid JSON string";
        break;
    case FLB_ERR_JSON_PART:
        msg = "Truncated JSON string";
        break;
    }

    if (!msg) {
        fprintf(stderr,
                "%sError%s: undefined. Aborting",
                ANSI_BOLD ANSI_RED, ANSI_RESET);
    }
    else {
        fprintf(stderr,
                "%sError%s: %s. Aborting\n\n",
                ANSI_BOLD ANSI_RED, ANSI_RESET, msg);
    }

    if (err <= FLB_ERR_FILTER_INVALID) {
        exit(EXIT_FAILURE);
    }
}

/* Custom error */
void flb_utils_error_c(const char *msg)
{
    fprintf(stderr,
            "%sError%s: %s. Aborting\n\n",
            ANSI_BOLD ANSI_RED, ANSI_RESET, msg);
    exit(EXIT_FAILURE);
}

void flb_utils_warn_c(const char *msg)
{
    fprintf(stderr,
            "%sWarning%s: %s",
            ANSI_BOLD ANSI_YELLOW, ANSI_RESET, msg);
}

#ifdef FLB_HAVE_FORK
/* Run current process in background mode */
int flb_utils_set_daemon(struct flb_config *config)
{
    pid_t pid;

    if ((pid = fork()) < 0){
		flb_error("Failed creating to switch to daemon mode (fork failed)");
        exit(EXIT_FAILURE);
	}

    if (pid > 0) { /* parent */
        exit(EXIT_SUCCESS);
    }

    /* set files mask */
    umask(0);

    /* Create new session */
    setsid();

    if (chdir("/") < 0) { /* make sure we can unmount the inherited filesystem */
        flb_error("Unable to unmount the inherited filesystem");
        exit(EXIT_FAILURE);
	}

    /* Our last STDOUT messages */
    flb_info("switching to background mode (PID=%lu)", getpid());

    fclose(stderr);
    fclose(stdout);

    return 0;
}
#endif

void flb_utils_print_setup(struct flb_config *config)
{
    struct mk_list *head;
    struct flb_input_plugin *plugin;
    struct flb_input_collector *collector;
    struct flb_input_instance *in;

    flb_info("Configuration");

    /* general */
    flb_info(" flush time     : %i seconds", config->flush);

    /* Inputs */
    flb_info(" input plugins  : ");
    mk_list_foreach(head, &config->inputs) {
        in = mk_list_entry(head, struct flb_input_instance, _head);
        flb_info("%s ", in->p->name);
    }

    /* Collectors */
    flb_info(" collectors     : ");
    mk_list_foreach(head, &config->collectors) {
        collector = mk_list_entry(head, struct flb_input_collector, _head);
        plugin = collector->instance->p;

        if (collector->seconds > 0) {
            flb_info("[%s %lus,%luns] ",
                     plugin->name,
                     collector->seconds,
                     collector->nanoseconds);
        }
        else {
            printf("[%s] ", plugin->name);
        }

    }
}

struct mk_list *flb_utils_split(char *line, int separator, int max_split)
{
    int i = 0;
    int count = 0;
    int val_len;
    int len;
    int end;
    char *val;
    struct mk_list *list;
    struct flb_split_entry *new;

    list = flb_malloc(sizeof(struct mk_list));
    if (!list) {
        flb_errno();
        return NULL;
    }
    mk_list_init(list);

    len = strlen(line);
    while (i < len) {
        end = mk_string_char_search(line + i, separator, len - i);
        if (end >= 0 && end + i < len) {
            end += i;

            if (i == (unsigned int) end) {
                i++;
                continue;
            }

            val = mk_string_copy_substr(line, i, end);
            val_len = end - i;
        }
        else {
            val = mk_string_copy_substr(line, i, len);
            val_len = len - i;
            end = len;

        }

        /* Create new entry */
        new = flb_malloc(sizeof(struct flb_split_entry));
        new->value = val;
        new->len = val_len;

        mk_list_add(&new->_head, list);
        i = end + 1;

        count++;
        if (count >= max_split && max_split > 0 && i < len) {
            new = flb_malloc(sizeof(struct flb_split_entry));
            new->value = mk_string_copy_substr(line, i, len);
            new->len   = len - i;
            mk_list_add(&new->_head, list);
            break;
        }
    }

    return list;
}

void flb_utils_split_free(struct mk_list *list)
{
    struct mk_list *tmp;
    struct mk_list *head;
    struct flb_split_entry *entry;

    mk_list_foreach_safe(head, tmp, list) {
        entry = mk_list_entry(head, struct flb_split_entry, _head);
        mk_list_del(&entry->_head);
        flb_free(entry->value);
        flb_free(entry);
    }

    flb_free(list);
}

/* When a timer expires, it needs some handling */
int flb_utils_timer_consume(flb_pipefd_t fd)
{
    int ret;
    uint64_t val;

    ret = flb_pipe_r(fd, &val, sizeof(val));
    if (ret == -1) {
        flb_errno();
        return -1;
    }

#ifdef __linux__
    /* A timer on linux must return an unisgned 64 bit number */
    if (ret == 0) {
        return -1;
    }
#endif

    return 0;
}

int flb_utils_pipe_byte_consume(flb_pipefd_t fd)
{
    int ret;
    uint64_t val;

    ret = flb_pipe_r(fd, &val, sizeof(val));
    if (ret == -1) {
        flb_errno();
        return -1;
    }

    return 0;
}

ssize_t flb_utils_size_to_bytes(char *size)
{
    int i;
    int len;
    int plen = 0;
    size_t val;
    char c;
    char tmp[3] = {0};
    int64_t KB = 1000;
    int64_t MB = 1000 * KB;
    int64_t GB = 1000 * MB;

    if (!size) {
        return -1;
    }

    len = strlen(size);
    val = atoll(size);

    if (len == 0) {
        return -1;
    }

    for (i = len - 1; i > 0; i--) {
        if (isdigit(size[i])) {
            break;
        }
        else {
            plen++;
        }
    }

    if (plen == 0) {
        return val;
    }
    else if (plen > 2) {
        return -1;
    }

    for (i = 0; i < plen; i++) {
        c = size[(len - plen) + i];
        tmp[i] = toupper(c);
    }

    if (plen == 2) {
        if (tmp[1] != 'B') {
            return -1;
        }
    }

    if (tmp[0] == 'K') {
        return (val * KB);
    }
    else if (tmp[0] == 'M') {
        return (val * MB);
    }
    else if (tmp[0] == 'G') {
        return (val * GB);
    }
    else {
        return -1;
    }

    return val;
}

int flb_utils_time_to_seconds(char *time)
{
    int len;
    size_t val;

    len = strlen(time);
    val = atoi(time);

    /* String time to seconds */
    if (time[len - 1] == 'D' || time[len - 1] == 'd') {
        val *= 86400;
    }
    if (time[len - 1] == 'H' || time[len - 1] == 'h') {
        val *= 3600;
    }
    else if (time[len - 1] == 'M' || time[len - 1] == 'm') {
        val *= 60;
    }

    return val;
}

int flb_utils_bool(char *val)
{
    if (strcasecmp(val, "true") == 0 ||
        strcasecmp(val, "on") == 0 ||
        strcasecmp(val, "yes") == 0) {
        return FLB_TRUE;
    }

    return FLB_FALSE;
}

/* Convert a 'string' time seconds.nanoseconds to int and long values */
int flb_utils_time_split(char *time, int *sec, long *nsec)
{
    char *p;
    char *end;
    long val = 0;

    errno = 0;
    val = strtol(time, &end, 10);
    if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
        || (errno != 0 && val == 0)) {
        flb_errno();
        return -1;
    }
    if (end == time) {
        return -1;
    }
    *sec = (int) val;

    /* Try to find subseconds */
    *nsec = 0;
    p = strchr(time, '.');
    if (p) {
        p += 1;
        val = strtol(p, &end, 10);
        if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
            || (errno != 0 && val == 0)) {
            flb_errno();
            return -1;
        }
        if (end == p) {
            return -1;
        }
        *nsec = val;
    }

    return 0;
}

void flb_utils_bytes_to_human_readable_size(size_t bytes,
                                            char *out_buf, size_t size)
{
    unsigned long i;
    unsigned long u = 1024;
    static const char *__units[] = {
        "b", "K", "M", "G",
        "T", "P", "E", "Z", "Y", NULL
    };

    for (i = 0; __units[i] != NULL; i++) {
        if ((bytes / u) == 0) {
            break;
        }
        u *= 1024;
    }
    if (!i) {
        snprintf(out_buf, size, "%lu%s", (long unsigned int) bytes, __units[0]);
    }
    else {
        float fsize = (float) ((double) bytes / (u / 1024));
        snprintf(out_buf, size, "%.1f%s", fsize, __units[i]);
    }
}

static void flb_service_conf_err(struct mk_rconf_section *section, char *key)
{
    fprintf(stderr, "Invalid configuration value at %s.%s\n",
        section->name, key);
}

static int flb_service_conf_path_set(struct flb_config *config, char *file)
{
    char *p;
    char *end;
    char path[PATH_MAX + 1];
#if defined(_WIN32) || defined(_WIN64)
    DWORD  retval = 0;

    retval = GetFullPathName(file, PATH_MAX + 1, path, NULL);

    if (retval == 0)
    {
        return -1;
    }
#else
    p = realpath(file, path);
    if (!p) {
        return -1;
    }
#endif
    /* lookup path ending and truncate */
    end = strrchr(path, '/');
    if (!end) {
        return -1;
    }

    end++;
    *end = '\0';
    config->conf_path = flb_strdup(path);

    return 0;
}

int flb_service_conf(struct flb_config *config, const char *file)
{
    int ret = -1;
    char *tmp;
    char *name;
    struct mk_list *head;
    struct mk_list *h_prop;
    struct mk_rconf *fconf = NULL;
    struct mk_rconf_entry *entry;
    struct mk_rconf_section *section;
    struct flb_input_instance *in;
    struct flb_output_instance *out;
    struct flb_filter_instance *filter;

    fconf = mk_rconf_open(file);
    if (!fconf) {
        return -1;
    }

    /* Process all meta commands */
    mk_list_foreach(head, &fconf->metas) {
        entry = mk_list_entry(head, struct mk_rconf_entry, _head);
        flb_meta_run(config, entry->key, entry->val);
    }

    /* Set configuration root path */
    flb_service_conf_path_set(config, file);

    /* Validate sections */
    mk_list_foreach(head, &fconf->sections) {
        section = mk_list_entry(head, struct mk_rconf_section, _head);

        if (strcasecmp(section->name, "SERVICE") == 0 ||
            strcasecmp(section->name, "INPUT") == 0 ||
            strcasecmp(section->name, "FILTER") == 0 ||
            strcasecmp(section->name, "OUTPUT") == 0) {

            /* continue on valid sections */
            continue;
        }

        /* Extra sanity checks */
        if (strcasecmp(section->name, "PARSER") == 0) {
            fprintf(stderr,
                "Section [PARSER] is not valid in the main "
                "configuration file. It belongs to \n"
                "the Parsers_File configuration files.\n");
        }
        else {
            fprintf(stderr,
                "Error: unexpected section [%s] in the main "
                "configuration file.\n", section->name);
        }
        exit(EXIT_FAILURE);
    }

    /* Read main [SERVICE] section */
    section = mk_rconf_section_get(fconf, "SERVICE");
    if (section) {
        /* Iterate properties */
        mk_list_foreach(h_prop, &section->entries) {
            entry = mk_list_entry(h_prop, struct mk_rconf_entry, _head);
            /* Set the property */
            flb_config_set_property(config, entry->key, entry->val);
        }
    }


    /* Read all [INPUT] sections */
    mk_list_foreach(head, &fconf->sections) {
        section = mk_list_entry(head, struct mk_rconf_section, _head);
        if (strcasecmp(section->name, "INPUT") != 0) {
            continue;
        }

        /* Get the input plugin name */
        name = s_get_key(section, "Name", MK_RCONF_STR);
        if (!name) {
            flb_service_conf_err(section, "Name");
            goto flb_service_conf_end;
        }

        flb_debug("[service] loading input: %s", name);

        /* Create an instace of the plugin */
        tmp = flb_env_var_translate(config->env, name);
        in = flb_input_new(config, tmp, NULL);
        mk_mem_free(name);
        if (!in) {
            fprintf(stderr, "Input plugin '%s' cannot be loaded\n", tmp);
            mk_mem_free(tmp);
            goto flb_service_conf_end;
        }
        mk_mem_free(tmp);

        /* Iterate other properties */
        mk_list_foreach(h_prop, &section->entries) {
            entry = mk_list_entry(h_prop, struct mk_rconf_entry, _head);
            if (strcasecmp(entry->key, "Name") == 0) {
                continue;
            }

            /* Set the property */
            ret = flb_input_set_property(in, entry->key, entry->val);
            if (ret == -1) {
                fprintf(stderr, "Error setting up %s plugin property '%s'\n",
                    in->name, entry->key);
                goto flb_service_conf_end;
            }
        }
    }

    /* Read all [OUTPUT] sections */
    mk_list_foreach(head, &fconf->sections) {
        section = mk_list_entry(head, struct mk_rconf_section, _head);
        if (strcasecmp(section->name, "OUTPUT") != 0) {
            continue;
        }

        /* Get the output plugin name */
        name = s_get_key(section, "Name", MK_RCONF_STR);
        if (!name) {
            flb_service_conf_err(section, "Name");
            goto flb_service_conf_end;
        }

        /* Create an instace of the plugin */
        tmp = flb_env_var_translate(config->env, name);
        out = flb_output_new(config, tmp, NULL);
        mk_mem_free(name);
        if (!out) {
            fprintf(stderr, "Output plugin '%s' cannot be loaded\n", tmp);
            mk_mem_free(tmp);
            goto flb_service_conf_end;
        }
        mk_mem_free(tmp);

        /* Iterate other properties */
        mk_list_foreach(h_prop, &section->entries) {
            entry = mk_list_entry(h_prop, struct mk_rconf_entry, _head);
            if (strcasecmp(entry->key, "Name") == 0) {
                continue;
            }

            /* Set the property */
            flb_output_set_property(out, entry->key, entry->val);
        }
    }

    /* Read all [FILTER] sections */
    mk_list_foreach(head, &fconf->sections) {
        section = mk_list_entry(head, struct mk_rconf_section, _head);
        if (strcasecmp(section->name, "FILTER") != 0) {
            continue;
        }
        /* Get the filter plugin name */
        name = s_get_key(section, "Name", MK_RCONF_STR);
        if (!name) {
            flb_service_conf_err(section, "Name");
            goto flb_service_conf_end;
        }
        /* Create an instace of the plugin */
        tmp = flb_env_var_translate(config->env, name);
        filter = flb_filter_new(config, tmp, NULL);
        mk_mem_free(tmp);
        mk_mem_free(name);
        if (!filter) {
            flb_service_conf_err(section, "Name");
            goto flb_service_conf_end;
        }

        /* Iterate other properties */
        mk_list_foreach(h_prop, &section->entries) {
            entry = mk_list_entry(h_prop, struct mk_rconf_entry, _head);
            if (strcasecmp(entry->key, "Name") == 0) {
                continue;
            }

            /* Set the property */
            flb_filter_set_property(filter, entry->key, entry->val);
        }
    }

    ret = 0;

flb_service_conf_end:
    if (fconf != NULL) {
        mk_rconf_free(fconf);
    }
    return ret;
}