#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "builtins.h"

#define MAX_INPUT 1024

static int debug_ast = 0;
static int interactive = 1;
extern ParserState *current_parser_state;

int main(int argc, char *argv[]) {
    char input[MAX_INPUT];
    ParserState state;
    Environment env;
    FunctionTable ft;
    int last_exit_status = 0;

    init_parser_state(&state);
    init_environment(&env, argc > 0 ? argv[0] : NULL, argc, argv);
    init_function_table(&ft);
    builtin_alias("ll", "ls -l");
    builtin_alias("dir", "ls dir ");

    // if (!isatty(fileno(stdin))) interactive = 0;
    interactive = 1;
    current_parser_state = &state;

    while (1) {
        if (interactive) {
            printf(state.expecting ? ">> " : "> ");
            fflush(stdout);
        }
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin)) break;
            perror("fgets failed");
            continue;
        }
        input[strcspn(input, "\n")] = 0;

        ASTNode *ast = NULL;
        ParseStatus status = parse_line(input, &state, &ast);

        switch (status) {
            case PARSE_COMPLETE:
                if (ast) {
                    if (debug_ast) print_ast(ast, 0);
                    ExecStatus exec_status = execute_ast(ast, &env, &ft, &last_exit_status);
                    free_ast(ast);
                    if (!interactive && exec_status == EXEC_RETURN) {
                        free_parser_state(&state);
                        free_environment(&env);
                        free_function_table(&ft);
                        exit(last_exit_status);
                    }
                }
                state.token_count = 0;
                state.brace_depth = 0;
                state.paren_depth = 0;
                state.expecting = 0;
                break;
            case PARSE_INCOMPLETE:
                if (ast) free_ast(ast);
                break;
            case PARSE_ERROR:
                state.token_count = 0;
                state.brace_depth = 0;
                state.paren_depth = 0;
                state.expecting = 0;
                break;
        }
    }

    free_parser_state(&state);
    free_environment(&env);
    free_function_table(&ft);
    return last_exit_status;
}
