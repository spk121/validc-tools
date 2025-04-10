#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "functions.h"
#include "parser.h"
#include "builtins.h"

#define MAX_INPUT 1024

static int debug_ast = 1; // Default off

void print_ast(ASTNode *node, int depth) {
    if (!node) return;
    for (int i = 0; i < depth; i++) printf("  ");
    switch (node->type) {
        case AST_SIMPLE_COMMAND:
            printf("SIMPLE_COMMAND\n");
            for (int i = 0; i < node->data.simple_command.prefix_count; i++) {
                for (int j = 0; j < depth + 1; j++) printf("  ");
                printf("PREFIX: %s\n", node->data.simple_command.prefix[i]);
            }
            if (node->data.simple_command.command) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("COMMAND: %s\n", node->data.simple_command.command);
            }
            for (int i = 0; i < node->data.simple_command.suffix_count; i++) {
                for (int j = 0; j < depth + 1; j++) printf("  ");
                printf("SUFFIX: %s\n", node->data.simple_command.suffix[i]);
            }
            Redirect *redir = node->data.simple_command.redirects;
            while (redir) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("REDIRECT: %s%s %s\n", redir->io_number ? redir->io_number : "",
                       redir->operator == LESS ? "<" : redir->operator == GREAT ? ">" : "??", redir->filename);
                redir = redir->next;
            }
            break;
        case AST_PIPELINE:
            printf("PIPELINE (bang: %d)\n", node->data.pipeline.bang);
            for (int i = 0; i < node->data.pipeline.command_count; i++) {
                print_ast(node->data.pipeline.commands[i], depth + 1);
            }
            break;
        case AST_AND_OR:
            printf("AND_OR (%s)\n", node->data.and_or.operator == AND_IF ? "&&" : "||");
            print_ast(node->data.and_or.left, depth + 1);
            print_ast(node->data.and_or.right, depth + 1);
            break;
        case AST_LIST:
            printf("LIST (sep: %s)\n", node->data.list.separator == SEMI ? ";" : node->data.list.separator == AMP ? "&" : node->data.list.separator == NEWLINE ? "\\n" : "none");
            print_ast(node->data.list.and_or, depth + 1);
            print_ast(node->data.list.next, depth + 1);
            break;
        case AST_COMPLETE_COMMAND:
            printf("COMPLETE_COMMAND (sep: %s)\n", node->data.complete_command.separator == SEMI ? ";" : node->data.complete_command.separator == AMP ? "&" : "none");
            print_ast(node->data.complete_command.list, depth + 1);
            break;
        case AST_PROGRAM:
            printf("PROGRAM\n");
            print_ast(node->data.program.commands, depth + 1);
            break;
        case AST_IF_CLAUSE:
            printf("IF_CLAUSE\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("CONDITION:\n");
            print_ast(node->data.if_clause.condition, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("THEN:\n");
            print_ast(node->data.if_clause.then_body, depth + 2);
            if (node->data.if_clause.else_part) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("ELSE:\n");
                print_ast(node->data.if_clause.else_part, depth + 2);
            }
            break;
        case AST_FOR_CLAUSE:
            printf("FOR_CLAUSE (var: %s)\n", node->data.for_clause.variable);
            if (node->data.for_clause.wordlist_count > 0) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("WORDLIST:");
                for (int i = 0; i < node->data.for_clause.wordlist_count; i++) {
                    printf(" %s", node->data.for_clause.wordlist[i]);
                }
                printf("\n");
            }
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("BODY:\n");
            print_ast(node->data.for_clause.body, depth + 2);
            break;
        case AST_CASE_CLAUSE:
            printf("CASE_CLAUSE (word: %s)\n", node->data.case_clause.word);
            CaseItem *item = node->data.case_clause.items;
            while (item) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("ITEM (patterns:");
                for (int j = 0; j < item->pattern_count; j++) {
                    printf(" %s", item->patterns[j]);
                }
                printf(") (dsemi: %d)\n", item->has_dsemi);
                if (item->action) print_ast(item->action, depth + 2);
                item = item->next;
            }
            break;
        case AST_WHILE_CLAUSE:
            printf("WHILE_CLAUSE\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("CONDITION:\n");
            print_ast(node->data.while_clause.condition, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("BODY:\n");
            print_ast(node->data.while_clause.body, depth + 2);
            break;
        case AST_UNTIL_CLAUSE:
            printf("UNTIL_CLAUSE\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("CONDITION:\n");
            print_ast(node->data.until_clause.condition, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("BODY:\n");
            print_ast(node->data.until_clause.body, depth + 2);
            break;
        case AST_BRACE_GROUP:
            printf("BRACE_GROUP\n");
            print_ast(node->data.brace_group.body, depth + 1);
            break;
        case AST_SUBSHELL:
            printf("SUBSHELL\n");
            print_ast(node->data.subshell.body, depth + 1);
            break;
        case AST_FUNCTION_DEFINITION:
            printf("FUNCTION_DEFINITION (name: %s)\n", node->data.function_definition.name);
            print_ast(node->data.function_definition.body, depth + 1);
            redir = node->data.function_definition.redirects;
            while (redir) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("REDIRECT: %s%s %s\n", redir->io_number ? redir->io_number : "",
                       redir->operator == LESS ? "<" : redir->operator == GREAT ? ">" : "??", redir->filename);
                redir = redir->next;
            }
            break;
        case AST_IO_REDIRECT:
            printf("IO_REDIRECT: %s%s %s\n", node->data.io_redirect.io_number ? node->data.io_redirect.io_number : "",
                   node->data.io_redirect.operator == LESS ? "<" : node->data.io_redirect.operator == GREAT ? ">" : "??",
                   node->data.io_redirect.filename);
            break;
    }
}

int main() {
    char input[MAX_INPUT];
    ParserState state;
    Environment env;
    FunctionTable ft;
    int last_exit_status = 0;

    init_parser_state(&state);
    init_environment(&env);
    init_function_table(&ft);
    builtin_alias("ll", "ls -l");
    builtin_alias("dir", "ls dir ");

    while (1) {
        printf(state.expecting ? ">> " : "> ");
        fflush(stdout);
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
                    execute_ast(ast, &env, &ft, &last_exit_status);
                    free_ast(ast);
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
    return 0;
}
