/********************************************************************
 *  Mini Batch Shell – Modular version with dispatch table
 *  Built-ins: var, unset, vars, clearvars  (easily extensible)
 *
 *  Variable assignment operators (after variable name):
 *    =    assign literal text (no expansion; braces kept)
 *    :=   assign with immediate {{var}} expansion
 *    +=   append literal text (no expansion)
 *    +:=  append with immediate {{var}} expansion
 *
 *  Conditional execution (no nesting allowed):
 *    ifeq <arg1> <arg2>
 *      <commands>
 *    endif
 *
 *    ifneq <arg1> <arg2>
 *      <commands>
 *    endif
 *
 *  Args can be:
 *    - {{var}}             (expanded)
 *    - text                (unquoted, taken literally, trimmed trailing ws)
 *    - "text in quotes"    (quoted, supports escapes: \" and \\)
 *
 *  Examples:
 *    var foo = hello {{world}}         # foo becomes: hello {{world}}
 *    var world = Earth
 *    var foo := hello {{world}}        # foo becomes: hello Earth
 *    var foo += more {{braces}}        # foo becomes: (previous) + " more {{braces}}"
 *    var foo +:= and {{world}} again   # expansions applied in appended part
 *
 *    ifeq {{world}} "Earth"
 *      echo OK
 *    endif
 *
 *    ifneq {{world}} Mars
 *      echo Not Mars
 *    endif
 ********************************************************************/

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 1024
#define MAX_EXPANDED (MAX_LINE * 8)
#define MAX_VARS 100
#define MAX_VAR_NAME 32
#define MAX_VAR_VALUE 256

typedef struct
{
    char name[MAX_VAR_NAME];
    char value[MAX_VAR_VALUE];
} Variable;

/* Global state */
static Variable vars[MAX_VARS];
static size_t var_count = 0;
static bool verbose_flag = false;
static bool dry_run_flag = false;
static bool undefined_error_flag = false;

/*==================================================================*/
/* Helper functions                                                 */
/*==================================================================*/

static bool is_valid_var_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '-';
}

static void strip_trailing_ws(char *s)
{
    char *p = s + strlen(s);
    while (p > s && isspace((unsigned char)(p[-1])))
        --p;
    *p = '\0';
}

/* Find variable index, returns var_count if not found */
static size_t var_find(const char *name)
{
    for (size_t i = 0; i < var_count; ++i)
        if (strcmp(vars[i].name, name) == 0)
            return i;
    return var_count;
}

/* Ensure variable exists; create if needed. Return index or var_count on failure */
static size_t var_ensure(const char *name)
{
    size_t idx = var_find(name);
    if (idx < var_count)
        return idx;
    if (var_count >= MAX_VARS)
        return var_count;
    idx = var_count++;
    strncpy(vars[idx].name, name, MAX_VAR_NAME - 1);
    vars[idx].name[MAX_VAR_NAME - 1] = '\0';
    vars[idx].value[0] = '\0';
    return idx;
}

/*==================================================================*/
/* Variable expansion {{name}}                                      */
/*==================================================================*/

static bool expand_variables(const char *in, char *out, size_t outsz)
{
    size_t pos = 0;
    while (*in && pos < outsz - 1)
    {
        if (in[0] == '{' && in[1] == '{')
        {
            in += 2;
            char vname[MAX_VAR_NAME] = {0};
            size_t n = 0;
            while (*in && !(in[0] == '}' && in[1] == '}') && n < MAX_VAR_NAME - 1)
                vname[n++] = *in++;

            if (*in == '}' && in[1] == '}')
            {
                in += 2;
                size_t idx = var_find(vname);
                if (idx < var_count)
                {
                    size_t vlen = strlen(vars[idx].value);
                    if (pos + vlen >= outsz)
                    {
                        fprintf(stderr, "Error: expansion result too long\n");
                        return false;
                    }
                    strcpy(out + pos, vars[idx].value);
                    pos += vlen;
                }
                else
                {
                    if (verbose_flag)
                        printf("(undefined variable: %s)\n", vname);
                    if (undefined_error_flag)
                    {
                        fprintf(stderr, "Error: undefined variable '%s'\n", vname);
                        return false;
                    }
                    /* Undefined expands to empty if -u not set */
                }
            }
            else
            {
                fprintf(stderr, "Error: unmatched '{{'\n");
                return false;
            }
        }
        else
        {
            out[pos++] = *in++;
        }
    }
    out[pos] = '\0';
    return true;
}

/*==================================================================*/
/* Built-in command implementations                                 */
/*==================================================================*/

static int cmd_var(const char *line)
{
    const char *p = line;
    /* skip initial spaces and the word "var" */
    while (isspace((unsigned char)*p))
        ++p;
    while (*p && !isspace((unsigned char)*p))
        ++p; /* skip "var" */
    while (isspace((unsigned char)*p))
        ++p;

    /* extract name */
    char name[MAX_VAR_NAME] = {0};
    size_t n = 0;
    while (is_valid_var_char(*p) && n < MAX_VAR_NAME - 1)
        name[n++] = *p++;
    if (n == 0)
    {
        fprintf(stderr, "var: missing variable name\n");
        return -1;
    }
    while (isspace((unsigned char)*p))
        ++p;

    /* parse operator: '=', ':=', '+=', '+:=' */
    char op[4] = {0};
    const char *op_start = p;
    size_t o = 0;

    if (*p == '+')
    {
        op[o++] = *p++;
        if (*p == ':')
        {
            op[o++] = *p++;
        }
        if (*p == '=')
        {
            op[o++] = *p++;
        }
        else
        {
            fprintf(stderr, "var: expected '=' after '%.*s'\n", (int)(p - op_start), op_start);
            return -1;
        }
    }
    else if (*p == ':')
    {
        op[o++] = *p++;
        if (*p == '=')
            op[o++] = *p++;
        else
        {
            fprintf(stderr, "var: expected '=' after ':'\n");
            return -1;
        }
    }
    else if (*p == '=')
    {
        op[o++] = *p++;
    }
    else
    {
        fprintf(stderr, "var: expected assignment operator (=, :=, +=, +:=)\n");
        return -1;
    }
    op[o] = '\0';

    while (isspace((unsigned char)*p))
        ++p;

    /* parse value: quoted or unquoted */
    char raw_value[MAX_VAR_VALUE] = {0};
    size_t vpos = 0;

    if (*p == '"')
    {
        ++p;
        while (*p && vpos < MAX_VAR_VALUE - 1)
        {
            if (*p == '\\')
            {
                ++p;
                if (*p == '"' || *p == '\\')
                {
                    raw_value[vpos++] = *p++;
                }
                else
                {
                    if (*p)
                        raw_value[vpos++] = *p++;
                }
            }
            else if (*p == '"')
            {
                ++p;
                break;
            }
            else
            {
                raw_value[vpos++] = *p++;
            }
        }
        raw_value[vpos] = '\0';
        if (*(p - 1) != '"' && vpos < MAX_VAR_VALUE - 1)
        {
            fprintf(stderr, "var: missing closing quote\n");
            return -1;
        }
        if (vpos >= MAX_VAR_VALUE - 1 && *p && *p != '"')
        {
            fprintf(stderr, "var: value truncated to %d bytes\n", MAX_VAR_VALUE - 1);
        }
    }
    else
    {
        const char *start = p;
        size_t remaining = strlen(start);
        if (remaining >= MAX_VAR_VALUE)
        {
            strncpy(raw_value, start, MAX_VAR_VALUE - 1);
            raw_value[MAX_VAR_VALUE - 1] = '\0';
            fprintf(stderr, "var: value truncated to %d bytes\n", MAX_VAR_VALUE - 1);
        }
        else
        {
            strncpy(raw_value, start, MAX_VAR_VALUE - 1);
            raw_value[MAX_VAR_VALUE - 1] = '\0';
        }
        strip_trailing_ws(raw_value);
    }

    bool is_append = (strcmp(op, "+=") == 0 || strcmp(op, "+:=") == 0);
    bool do_expand = (strcmp(op, ":=") == 0 || strcmp(op, "+:=") == 0);

    char expanded_value[MAX_VAR_VALUE];
    const char *final_piece = raw_value;

    if (do_expand)
    {
        if (!expand_variables(raw_value, expanded_value, sizeof(expanded_value)))
        {
            /* expand_variables already printed an error message */
            return -1;
        }
        final_piece = expanded_value;
    }

    size_t idx;
    if (is_append)
    {
        idx = var_find(name);
        if (idx == var_count)
        {
            /* create new variable if appending to non-existent */
            idx = var_ensure(name);
            if (idx == var_count)
            {
                fprintf(stderr, "var: too many variables\n");
                return -1;
            }
        }
        size_t cur_len = strlen(vars[idx].value);
        size_t add_len = strlen(final_piece);
        if (cur_len + add_len >= MAX_VAR_VALUE)
        {
            size_t room = MAX_VAR_VALUE - 1 - cur_len;
            if (room > 0)
            {
                strncat(vars[idx].value, final_piece, room);
            }
            vars[idx].value[MAX_VAR_VALUE - 1] = '\0';
            fprintf(stderr, "var: appended value truncated by %zu bytes\n", (size_t)(add_len - room));
        }
        else
        {
            strcat(vars[idx].value, final_piece);
        }
    }
    else
    {
        idx = var_ensure(name);
        if (idx == var_count)
        {
            fprintf(stderr, "var: too many variables\n");
            return -1;
        }
        size_t len = strlen(final_piece);
        if (len >= MAX_VAR_VALUE)
        {
            strncpy(vars[idx].value, final_piece, MAX_VAR_VALUE - 1);
            vars[idx].value[MAX_VAR_VALUE - 1] = '\0';
            fprintf(stderr, "var: value truncated to %d bytes\n", MAX_VAR_VALUE - 1);
        }
        else
        {
            strncpy(vars[idx].value, final_piece, MAX_VAR_VALUE - 1);
            vars[idx].value[MAX_VAR_VALUE - 1] = '\0';
        }
    }

    return 0;
}

static int cmd_unset(const char *line)
{
    const char *p = line;
    while (*p && !isspace((unsigned char)*p))
        ++p; // skip "unset"
    while (isspace((unsigned char)*p))
        ++p;

    char name[MAX_VAR_NAME] = {0};
    size_t n = 0;
    while (is_valid_var_char(*p) && n < MAX_VAR_NAME - 1)
        name[n++] = *p++;
    if (n == 0)
    {
        fprintf(stderr, "unset: missing variable name\n");
        return -1;
    }

    size_t idx = var_find(name);
    if (idx == var_count)
    {
        if (verbose_flag)
            printf("unset: %s not defined\n", name);
        return 0;
    }

    /* remove by moving last element */
    vars[idx] = vars[--var_count];
    memset(&vars[var_count], 0, sizeof(Variable));
    return 0;
}

static int cmd_vars(const char *line)
{
    (void)line; // unused
    if (var_count == 0)
    {
        printf("(no variables)\n");
    }
    else
    {
        for (size_t i = 0; i < var_count; ++i)
        {
            printf("%s=\"%s\"\n", vars[i].name, vars[i].value);
        }
    }
    return 0;
}

static int cmd_clearvars(const char *line)
{
    (void)line; // unused
    memset(vars, 0, sizeof(vars));
    var_count = 0;
    if (verbose_flag)
        printf("All variables cleared\n");
    return 0;
}

/*==================================================================*/
/* Dispatch table                                                   */
/*==================================================================*/

typedef int (*builtin_fn)(const char *full_line);

typedef struct
{
    const char *name;
    builtin_fn fn;
} Builtin;

static const Builtin builtins[] = {{"var", cmd_var},
                                   {"unset", cmd_unset},
                                   {"vars", cmd_vars},
                                   {"clearvars", cmd_clearvars},
                                   /* terminator */
                                   {NULL, NULL}};

/* Returns pointer to function or NULL if not a built-in */
static builtin_fn find_builtin(const char *cmd)
{
    for (const Builtin *b = builtins; b->name; ++b)
    {
        size_t len = strlen(b->name);
        if (strncmp(cmd, b->name, len) == 0 && (cmd[len] == '\0' || isspace((unsigned char)cmd[len])))
            return b->fn;
    }
    return NULL;
}

/*==================================================================*/
/* Parsing helpers for conditionals                                 */
/*==================================================================*/

static const char *skip_ws(const char *p)
{
    while (isspace((unsigned char)*p))
        ++p;
    return p;
}

static bool parse_one_arg(const char **pp, char *out, size_t outsz)
{
    const char *p = skip_ws(*pp);
    size_t pos = 0;

    if (*p == '\0')
        return false;

    if (*p == '"')
    {
        ++p;
        while (*p && pos < outsz - 1)
        {
            if (*p == '\\')
            {
                ++p;
                if (*p == '"' || *p == '\\')
                    out[pos++] = *p++;
                else
                {
                    if (*p)
                        out[pos++] = *p++;
                }
            }
            else if (*p == '"')
            {
                ++p;
                break;
            }
            else
            {
                out[pos++] = *p++;
            }
        }
        if (*(p - 1) != '"' && pos < outsz - 1)
        {
            fprintf(stderr, "if*: missing closing quote\n");
            return false;
        }
    }
    else
    {
        /* read until whitespace */
        while (*p && !isspace((unsigned char)*p) && pos < outsz - 1)
            out[pos++] = *p++;
        /* trailing ws not inside quotes isn't part of arg; keep literal as-is */
    }

    out[pos] = '\0';
    *pp = p;
    return true;
}

/* Expand {{var}} inside an argument token (arg may contain braces) */
static bool expand_arg_token(const char *arg, char *expanded, size_t expsz)
{
    return expand_variables(arg, expanded, expsz);
}

/*==================================================================*/
/* Main loop                                                        */
/*==================================================================*/

int main(int argc, char *argv[])
{
    bool ignore_errors = false;
    bool interactive = false;
    FILE *input = stdin;
    const char *filename = NULL;

    /* Argument parsing */
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            printf("Usage: batch [options] [script]\n"
                   "\n"
                   "Built-ins:\n"
                   "  var NAME = VALUE        Set literal VALUE (no expansion).\n"
                   "  var NAME := VALUE       Set VALUE with immediate {{VAR}} expansion.\n"
                   "  var NAME += VALUE       Append literal VALUE.\n"
                   "  var NAME +:= VALUE      Append VALUE with expansion.\n"
                   "     VALUE may be quoted; inside quotes \\\" and \\\\ are escapes.\n"
                   "     Unquoted VALUE strips trailing whitespace.\n"
                   "  unset NAME              Unset variable.\n"
                   "  vars                    List all variables.\n"
                   "  clearvars               Remove all variables.\n"
                   "\n"
                   "Conditionals (no nesting allowed):\n"
                   "  ifeq <arg1> <arg2>      Execute following commands until 'endif' if arg1 == arg2.\n"
                   "  ifneq <arg1> <arg2>     Execute following commands until 'endif' if arg1 != arg2.\n"
                   "    Args can be {{var}}, text, or \"quoted text\" with escapes \\\" and \\\\.\n"
                   "    Encountering ifeq/ifneq inside an active if-block is an error.\n"
                   "\n"
                   "Expansion in commands:\n"
                   "  {{NAME}} expands to variable value; unmatched '{{' is error.\n"
                   "  Undefined variables expand to empty unless -u is set.\n"
                   "\n"
                   "Options:\n"
                   "  -v    Verbose output.\n"
                   "  -i    Ignore non-zero command exit status and continue.\n"
                   "  -n    Dry run: print commands without executing.\n"
                   "  -u    Treat undefined variables in expansions as errors.\n"
                   "  -h, --help  Show this help.\n");
            return 0;
        }
        else if (!strcmp(argv[i], "-v"))
            verbose_flag = true;
        else if (!strcmp(argv[i], "-i"))
            ignore_errors = true;
        else if (!strcmp(argv[i], "-n"))
            dry_run_flag = true;
        else if (!strcmp(argv[i], "-u"))
            undefined_error_flag = true;
        else if (argv[i][0] == '-')
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
        else if (!filename)
            filename = argv[i];
        else
        {
            fprintf(stderr, "Too many arguments\n");
            return 1;
        }
    }

    if (filename)
    {
        input = fopen(filename, "r");
        if (!input)
        {
            perror(filename);
            return 1;
        }
    }
    else
    {
        interactive = true;
    }

    char line[MAX_LINE];
    char full_cmd[MAX_LINE * 4] = {0};
    char expanded[MAX_EXPANDED];
    bool continuing = false;

    /* Conditional state */
    bool in_if_block = false;
    bool execute_if_block = false;

    while (1)
    {
        if (interactive && !continuing)
        {
            printf("> ");
            fflush(stdout);
        }

        if (!fgets(line, sizeof(line), input))
        {
            if (ferror(input))
                perror("read");
            if (continuing)
                fprintf(stderr, "Unterminated continuation\n");
            break;
        }

        line[strcspn(line, "\r\n")] = '\0';

        if (!continuing && (line[0] == ';' || line[0] == '\0'))
            continue;

        bool cont = false;
        size_t len = strlen(line);
        if (len && line[len - 1] == '\\')
        {
            line[len - 1] = '\0';
            strip_trailing_ws(line);
            cont = true;
        }

        if (continuing)
        {
            size_t cur = strlen(full_cmd);
            if (cur)
            {
                if (cur + 1 >= sizeof(full_cmd))
                {
                    fprintf(stderr, "Continuation: command buffer full, truncating space\n");
                }
                else
                {
                    strcat(full_cmd, " ");
                }
            }
            size_t room = sizeof(full_cmd) - strlen(full_cmd) - 1;
            size_t to_copy = strlen(line);
            if (to_copy > room)
            {
                fprintf(stderr, "Continuation: command truncated by %zu bytes\n", to_copy - room);
                to_copy = room;
            }
            strncat(full_cmd, line, to_copy);
        }
        else
        {
            size_t to_copy = strlen(line);
            if (to_copy >= sizeof(full_cmd))
            {
                fprintf(stderr, "Command too long, truncated by %zu bytes\n", to_copy - (sizeof(full_cmd) - 1));
                to_copy = sizeof(full_cmd) - 1;
            }
            memcpy(full_cmd, line, to_copy);
            full_cmd[to_copy] = '\0';
        }

        if (cont)
        {
            continuing = true;
            continue;
        }
        continuing = false;

        char *cmd = full_cmd;
        while (isspace((unsigned char)*cmd))
            cmd++;

        if (*cmd == '\0')
        {
            full_cmd[0] = '\0';
            continue;
        }

        /* Handle conditionals first */
        if (!strncmp(cmd, "ifeq", 4) && (cmd[4] == '\0' || isspace((unsigned char)cmd[4])))
        {
            if (in_if_block)
            {
                fprintf(stderr, "Error: nested 'ifeq' inside if-block\n");
                break;
            }
            const char *p = cmd + 4;
            char a1[MAX_VAR_VALUE] = {0}, a2[MAX_VAR_VALUE] = {0};
            char e1[MAX_VAR_VALUE] = {0}, e2[MAX_VAR_VALUE] = {0};

            if (!parse_one_arg(&p, a1, sizeof(a1)) || !parse_one_arg(&p, a2, sizeof(a2)))
            {
                fprintf(stderr, "ifeq: expected two arguments\n");
                break;
            }
            if (!expand_arg_token(a1, e1, sizeof(e1)) || !expand_arg_token(a2, e2, sizeof(e2)))
            {
                /* expand_variables already printed error (including unmatched {{ or undefined error when -u) */
                break;
            }

            in_if_block = true;
            execute_if_block = (strcmp(e1, e2) == 0);
            if (verbose_flag)
                printf("ifeq: '%s' %s '%s'\n", e1, execute_if_block ? "==" : "!=", e2);

            full_cmd[0] = '\0';
            continue;
        }
        else if (!strncmp(cmd, "ifneq", 5) && (cmd[5] == '\0' || isspace((unsigned char)cmd[5])))
        {
            if (in_if_block)
            {
                fprintf(stderr, "Error: nested 'ifneq' inside if-block\n");
                break;
            }
            const char *p = cmd + 5;
            char a1[MAX_VAR_VALUE] = {0}, a2[MAX_VAR_VALUE] = {0};
            char e1[MAX_VAR_VALUE] = {0}, e2[MAX_VAR_VALUE] = {0};

            if (!parse_one_arg(&p, a1, sizeof(a1)) || !parse_one_arg(&p, a2, sizeof(a2)))
            {
                fprintf(stderr, "ifneq: expected two arguments\n");
                break;
            }
            if (!expand_arg_token(a1, e1, sizeof(e1)) || !expand_arg_token(a2, e2, sizeof(e2)))
            {
                /* error already printed */
                break;
            }

            in_if_block = true;
            execute_if_block = (strcmp(e1, e2) != 0);
            if (verbose_flag)
                printf("ifneq: '%s' %s '%s'\n", e1, execute_if_block ? "!=" : "==", e2);

            full_cmd[0] = '\0';
            continue;
        }
        else if (!strncmp(cmd, "endif", 5) && (cmd[5] == '\0' || isspace((unsigned char)cmd[5])))
        {
            if (!in_if_block)
            {
                fprintf(stderr, "Error: 'endif' without matching ifeq/ifneq\n");
                break;
            }
            in_if_block = false;
            execute_if_block = false;
            if (verbose_flag)
                printf("endif\n");
            full_cmd[0] = '\0';
            continue;
        }
        else if (in_if_block && !execute_if_block)
        {
            /* Inside an if-block that should not execute: skip all commands until endif */
            full_cmd[0] = '\0';
            continue;
        }

        /* Built-ins and external commands */
        builtin_fn builtin = find_builtin(cmd);
        if (builtin)
        {
            int r = builtin(full_cmd); // pass whole line for easier parsing
            if (verbose_flag)
                printf("builtin returned %d\n", r);
        }
        else
        {
            if (!expand_variables(cmd, expanded, sizeof(expanded)))
            {
                fprintf(stderr, "Expanded command error/too long, aborting\n");
                break;
            }

            if (dry_run_flag)
            {
                printf("Would run: %s\n", expanded);
            }
            else
            {
                if (verbose_flag || interactive)
                    printf("Run: %s\n", expanded);
                int r = system(expanded);
                if (verbose_flag || interactive)
                    printf("=> %d\n", r);
                if (r != 0 && !ignore_errors)
                {
                    fprintf(stderr, "Command failed, aborting\n");
                    break;
                }
            }
        }

        full_cmd[0] = '\0';
    }

    if (in_if_block)
    {
        fprintf(stderr, "Error: unterminated if-block (missing 'endif')\n");
    }

    if (input != stdin)
        fclose(input);
    return 0;
}
