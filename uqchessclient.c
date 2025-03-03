/**
 * CSSE2310 assignment 4, chess client
 * Ian Pinto
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <csse2310a4.h>
#include "shared.h"

int const invalidArgsExitCode = 13;
int const socketConnectExitCode = 11;
int const serverGoneExitCode = 8;

// Client command-line arguments
typedef struct {
    // Port service/number or NULL if not given
    char* port;
    // Game's opponent type
    Opponent opponent;
    // Colour playing as
    Colour colour;
} Args;

// State of a game (for client), booleans used to avoid invalid reads
typedef struct {
    // Whether game is in progress
    bool isGameInProgress;
    // Is it client's turn?
    bool isClientTurn;
    // Is client playing as white?
    bool isClientWhite;
} GameState;

// Data passed to stdin thread
typedef struct {
    Args args;
    GameState* gameState;
    FILE* readSocket;
    FILE* writeSocket;
} ThreadData;

/**
 * @brief Print invalid args message and exit with code invalidArgsExitCode.
 */
void warn_invalid_args(void)
{
    fprintf(stderr,
            "Usage: uqchessclient portnum [--versus computer|human] [--colour "
            "black|white]\n");
    fflush(stderr);
    exit(invalidArgsExitCode);
}

/**
 * @brief Print can't connect message and exit with code socketConnectExitCode.
 *
 * @param port serv name/port num used in the error msg
 */
void warn_socket_connect_error(char* port)
{
    fprintf(stderr, "uqchessclient: can't connect to port \"%s\"\n", port);
    fflush(stderr);
    exit(socketConnectExitCode);
}

/**
 * @brief Process a command-line option (starts with --) and update the given
 * args struct accordingly.
 *
 * @param option the option given on the command-line
 * @param nextArg the argument given after the option (option value)
 * @param args pointer to command-line args
 * @return -1 if option was invalid, 1 if valid and the next argument was also
 * checked, 0 otherwise
 */
int check_cl_option(char* option, char* nextArg, Args* args)
{
    if (!strcmp(option, "--versus")) {
        if (args->opponent != OPPONENT_UNSPECIFIED) {
            // Opponent already given
            return -1;
        }
        if (!strcmp(nextArg, "computer")) {
            args->opponent = OPPONENT_COM;
            return 1;
        }
        if (!strcmp(nextArg, "human")) {
            args->opponent = OPPONENT_HUMAN;
            return 1;
        }
        return -1;
    }
    if (!strcmp(option, "--colour")) {
        if (args->colour != COLOUR_UNSPECIFIED) {
            // Colour already given
            return -1;
        }
        if (!strcmp(nextArg, "black")) {
            args->colour = COLOUR_BLACK;
            return 1;
        }
        if (!strcmp(nextArg, "white")) {
            args->colour = COLOUR_WHITE;
            return 1;
        }
        return -1;
    }
    // Option wasn't opponent or colour, hence invalid
    return -1;
}

/**
 * @brief Set default client arguments. If no opponent given, set to computer.
 * If no colour given and playing computer, set to white.
 *
 * @param args pointer to command-line args
 */
void set_default_args(Args* args)
{
    if (args->opponent == OPPONENT_UNSPECIFIED) {
        args->opponent = OPPONENT_COM;
    }
    if (args->colour == COLOUR_UNSPECIFIED && args->opponent == OPPONENT_COM) {
        args->colour = COLOUR_WHITE;
    }
}

/**
 * @brief Process command-line arguments and returns an Args struct containing
 * info about the arguments.
 *
 * @param argc number of arguments
 * @param argv array of arguments
 * @return command-line arguments
 */
Args get_args(int argc, char** argv)
{
    Args args = {.port = NULL,
            .opponent = OPPONENT_UNSPECIFIED,
            .colour = COLOUR_UNSPECIFIED};
    if (argc == 1) {
        // Not enough arguments
        warn_invalid_args();
    }

    // First arg (port)
    char* firstArg = argv[1];
    if (strlen(firstArg) == 0) {
        warn_invalid_args();
    }
    args.port = firstArg;

    // Options
    for (int i = 2; i < argc; i++) {
        char* arg = argv[i];
        if (strlen(arg) == 0) {
            warn_invalid_args();
        }
        if (!is_option(arg) // doesn't start with --
                || i + 1 == argc // no option value after
        ) {
            warn_invalid_args();
        }
        int result = check_cl_option(arg, argv[i + 1], &args);
        if (result == -1) {
            warn_invalid_args();
        }
        i += result; // if check_cl_option() checked an option, it returned 1,
                     // so increment i to avoid checking the option's value
                     // again
    }

    set_default_args(&args);
    return args;
}

/**
 * @brief Send start msg to server.
 *
 * @param socket server socket to write to
 * @param opponent opponent type chosen
 * @param colour colour playing as
 */
void send_start(FILE* socket, Opponent opponent, Colour colour)
{
    char opponentName[maxBufferSize];
    char colourName[maxBufferSize];
    get_opponent_name(opponentName, opponent);
    get_colour_name(colourName, colour);
    fflush(socket); // todo delete
    fprintf(socket, "start %s %s\n", opponentName, colourName);
    fflush(socket);
}

/**
 * @brief Send hint msg to server
 *
 * @param socket server socket to write to
 * @param all send "hint all" if true, "hint best" if false
 */
void send_hint(FILE* socket, bool all)
{
    fprintf(socket, "hint %s\n", all ? "all" : "best");
    fflush(socket);
}

/**
 * @brief Send move msg to server
 *
 * @param socket server socket to write to
 * @param move move to send (alphanumeric string)
 */
void send_move(FILE* socket, char* move)
{
    fprintf(socket, "move %s\n", move);
    fflush(socket);
}

/**
 * @brief Print command not valid msg to stderr
 */
void warn_command_not_valid(void)
{
    fprintf(stderr, "Try again - command is not valid\n");
    fflush(stderr);
}

/**
 * @brief Print game not in progress msg to stderr
 */
void warn_game_not_in_progress(void)
{
    fprintf(stderr, "Invalid command - game not in progress\n");
    fflush(stderr);
}

/**
 * @brief Print not your turn msg to stderr
 */
void warn_not_your_turn(void)
{
    fprintf(stderr, "Invalid command - not your turn\n");
    fflush(stderr);
}

/**
 * @brief Check if game is in progress, print error if it isn't
 *
 * @param gameState game to check
 * @return whether game in progress
 */
bool check_game_in_progress(GameState* gameState)
{
    if (!(gameState->isGameInProgress)) {
        warn_game_not_in_progress();
        return false;
    }
    return true;
}

/**
 * @brief Check if game in progress and it is client turn, print error if needed
 *
 * @param gameState game to check
 * @return true if game in progress and it is client's turn, false otherwise
 */
bool check_is_client_turn(GameState* gameState)
{
    if (!check_game_in_progress(gameState)) {
        return false;
    }
    if (!(gameState->isClientTurn)) {
        warn_not_your_turn();
        return false;
    }
    return true;
}

/**
 * @brief Act on one field (word) input from stdin
 *
 * @param threadData data passed into thread
 * @param cmd the field given
 * @return whether the field is valid (recognised)
 */
bool stdin_one_field(ThreadData threadData, char* cmd)
{
    Args args = threadData.args;
    GameState* gameState = threadData.gameState;
    if (!strcmp(cmd, "newgame")) {
        send_start(threadData.writeSocket, args.opponent, args.colour);
    } else if (!strcmp(cmd, "print")) {
        if (check_game_in_progress(gameState)) {
            fprintf(threadData.writeSocket, "board\n");
            fflush(threadData.writeSocket);
        }
    } else if (!strcmp(cmd, "hint")) {
        if (check_is_client_turn(gameState)) {
            send_hint(threadData.writeSocket, false);
        }
    } else if (!strcmp(cmd, "possible")) {
        if (check_is_client_turn(gameState)) {
            send_hint(threadData.writeSocket, true);
        }
    } else if (!strcmp(cmd, "resign")) {
        if (check_game_in_progress(gameState)) {
            fprintf(threadData.writeSocket, "resign\n");
            fflush(threadData.writeSocket);
        }
    } else if (!strcmp(cmd, "quit")) {
        exit(EXIT_SUCCESS); // exit immediately, no free
    } else {
        return false;
    }
    return true;
}

/**
 * @brief Read from stdin and send messages to server accordingly. To be
 * executed in a thread
 *
 * @param data ThreadData* - ptr to struct containing game state, cl args
 * @return unused, will never return
 */
void* thread_read_stdin(void* data)
{
    ThreadData threadData = *(ThreadData*)data;
    GameState* gameState = threadData.gameState;
    free(data);

    // Read from stdin
    char buffer[maxBufferSize];
    char* readResult;
    while (readResult = fgets(buffer, maxBufferSize, stdin),
            readResult != NULL) {
        if (feof(stdin)) {
            // stdin closed, ignore partial command entered
            break;
        }
        if (validate_line(buffer) == -1) {
            warn_command_not_valid();
            continue;
        }
        char** fields = split_by_char(buffer, ' ', 0);
        char* cmd = fields[0];
        int numFields = count_fields(fields);
        bool valid = true;
        if (numFields == 1) {
            valid = stdin_one_field(threadData, cmd);
        } else if (numFields == 2) {
            if (!strcmp(cmd, "move")) {
                char* moveChosen = fields[1];
                bool validMove = (valid_move_length(strlen(moveChosen))
                        && str_is_alnum(moveChosen));
                if (!validMove) {
                    warn_command_not_valid();
                } else if (check_is_client_turn(gameState)) {
                    send_move(threadData.writeSocket, moveChosen);
                }
            } else {
                valid = false;
            }
        } else {
            valid = false;
        }
        free(fields);
        if (!valid) {
            warn_command_not_valid();
        }
    }

    exit(EXIT_SUCCESS);
}

/**
 * @brief Act on 2-3 field (word) input from server
 *
 * @param cmd first field of input
 * @param secondField second field of input
 * @param gameState current game state
 */
void server_long_input(char* cmd, char* secondField, GameState* gameState)
{
    if (!strcmp(cmd, "started")) {
        gameState->isGameInProgress = true;
        char* colourGiven = secondField;
        if (!strcmp(colourGiven, "white")) {
            gameState->isClientWhite = true;
            gameState->isClientTurn = true;
        } else if (!strcmp(colourGiven, "black")) {
            gameState->isClientWhite = false;
            gameState->isClientTurn = false;
        }
    }
    // do nothing for "error _", "moves ..."
    else if (!strcmp(cmd, "moved")) {
        gameState->isClientTurn = !(gameState->isClientTurn);
    } else if (!strcmp(cmd, "gameover")) {
        gameState->isGameInProgress = false;
    }
}

/**
 * @brief Thread that reads commands sent from server
 *
 * @param socket socket connected to server to read from
 * @param gameState current game state for client
 */
void thread_read_server(FILE* socket, GameState* gameState)
{
    // Assuming a null char is never read from the socket
    char buffer[maxBufferSize];
    char* readResult;
    while (readResult = fgets(buffer, maxBufferSize, socket),
            readResult != NULL) {
        if (!strcmp(buffer, "startboard\n") || !strcmp(buffer, "endboard\n")) {
            // no need to process or print these
            continue;
        }
        fprintf(stdout, "%s", buffer);
        fflush(stdout);
        if (feof(socket)) {
            // todo verify this is correct
            // characters were read but server has gone
            break;
        }
        if (validate_line(buffer) == -1) {
            // do nothing for invalid line from server
            continue;
        }
        char** fields = split_by_char(buffer, ' ', 0);
        char* cmd = fields[0];
        int numFields = count_fields(fields);
        if (numFields == shortLine) {
            if (!strcmp(cmd, "ok")) {
                gameState->isClientTurn = !(gameState->isClientTurn);
            }
            // do nothing for startboard/endboard, check
        } else if (numFields == mediumLine || numFields == longLine) {
            server_long_input(cmd, fields[1], gameState);
        }
        free(fields);
    }

    fprintf(stderr, "uqchessclient: server has gone away\n");
    fflush(stderr);
    exit(serverGoneExitCode);
}

int main(int argc, char** argv)
{
    // Get args
    Args args = get_args(argc, argv);

    // Connect to server
    int socketFd = get_socket_fd(args.port);
    if (socketFd == -1) {
        warn_socket_connect_error(args.port);
    }
    FILE* readSocket = fdopen(socketFd, "r");
    FILE* writeSocket = fdopen(socketFd, "w");

    // Initial messages to stdout and server
    printf("Welcome to UQChessClient - written by s4800658\n");
    fflush(stdout);
    send_start(writeSocket, args.opponent, args.colour);

    // Set up data
    GameState* gameState = (GameState*)malloc(sizeof(GameState));
    memset(gameState, 0, sizeof(*gameState));
    gameState->isGameInProgress = false;
    ThreadData* threadData = (ThreadData*)malloc(sizeof(ThreadData));
    threadData->args = args;
    threadData->gameState = gameState;
    threadData->readSocket = readSocket;
    threadData->writeSocket = writeSocket;

    // thread id of process reading stdin
    pthread_t stdinThreadId;
    pthread_create(&stdinThreadId, NULL, thread_read_stdin, threadData);
    thread_read_server(readSocket, gameState);
    // Either thread will just exit, never return, so no need to join/detach

    return EXIT_SUCCESS;
}