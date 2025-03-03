/**
 * CSSE2310 assignment 4, chess server
 * Ian Pinto
 */

// REF: entire file very similar to lecture server-multithreaded.c

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#include <csse2310a4.h>
#include "shared.h"

int const errorCommand = -1;
int const errorGame = -2;
int const errorTurn = -3;

int const numArgsDefaultPort = 1;
int const numArgsGivenPort = 3;

int const numPlayers = 2;

int const queueLength = 10;

int const invalidArgsExitCode = 8;
int const cantStartListeningExitCode = 20;
int const cantStartCommsExitCode = 4;

char const goPerft1[] = "go perft 1\n";
char const zero[] = "0";

// Server cmd line args
typedef struct Args {
    // Serv name/port num given on command line, NULL if not given yet
    char* portFromCmdLine;
} Args;

/**
 * @brief Print invalid args message for server and exit with code
 * invalidArgsExitCode.
 */
void warn_invalid_args(void)
{
    fprintf(stderr, "Usage: ./uqchessserver [--listenOn portno]\n");
    fflush(stderr);
    exit(invalidArgsExitCode);
}

/**
 * @brief Print can't start listening and exit with code
 * cantStartListeningExitCode.
 *
 * @param port serv name/port num used in the error msg
 */
void warn_cant_start_listening(char* port)
{
    fprintf(stderr, "uqchessserver: can't start listening on port \"%s\"\n",
            port);
    fflush(stderr);
    exit(cantStartListeningExitCode);
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
    Args args = {.portFromCmdLine = NULL};

    if (argc == numArgsDefaultPort) {
        args.portFromCmdLine = (char*)zero;
    } else if (argc == numArgsGivenPort) {
        if (strcmp(argv[1], "--listenOn") != 0 || strlen(argv[2]) == 0) {
            warn_invalid_args();
        }
        args.portFromCmdLine = argv[2];
    } else {
        warn_invalid_args();
    }

    return args;
}

/**
 * @brief Get IPv4 address info for given port
 *
 * @param addrInfo ptr to write the address info to
 * @param portName name of port/service
 * @return 0 on success, -1 on failure
 */
int get_ip_addr_info(struct addrinfo** addrInfo, const char* portName)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // listen on all IP addresses

    int err;
    if ((err = getaddrinfo(NULL, portName, &hints, addrInfo))) {
        // Could not determine address
        freeaddrinfo(*addrInfo);
        return -1;
    }
    return 0;
}

/**
 * @brief Listens on given port. Gets listening socket
 *
 * @param portName serv name/port num
 * @param listenFd write the socket listening fd here
 * @param portNum write the received port num here
 * @return 0 if successful, -1 if not
 */
int open_listen(const char* portName, int* listenFd, uint16_t* portNum)
{
    struct addrinfo* ai = 0;
    if (get_ip_addr_info(&ai, portName) == -1) {
        return -1;
    }

    // Create a socket
    int listenFdFromSocket
            = socket(AF_INET, SOCK_STREAM, 0); // 0=default protocol (TCP)
    if (listenFdFromSocket < 0) {
        // Error creating socket
        return -1;
    }

    // Bind socket to address
    if (bind(listenFdFromSocket, ai->ai_addr, sizeof(struct sockaddr)) < 0) {
        // Bind socket to address
        return -1;
    }

    // REF: taken from lecture net4.c, Which port did we get?
    struct sockaddr_in ad;
    memset(&ad, 0, sizeof(struct sockaddr_in));
    socklen_t len = sizeof(struct sockaddr_in);
    if (getsockname(listenFdFromSocket, (struct sockaddr*)&ad, &len)) {
        // Getting sockname
        return -1;
    }
    *portNum = ntohs(ad.sin_port);

    // Allow address (port number) to be reused immediately
    int optVal = 1;
    if (setsockopt(listenFdFromSocket, SOL_SOCKET, SO_REUSEADDR, &optVal,
                sizeof(int))
            < 0) {
        // Error setting socket option
        return -1;
    }

    // Indicate willingness to listen on socket - connections can now be queued.
    // Up to 10 connection requests can queue (Reality on moss is that this
    // queue length parameter is ignored)
    if (listen(listenFdFromSocket, queueLength) < 0) {
        // Error listening
        return -1;
    }

    *listenFd = listenFdFromSocket;
    return 0; // no issues
}

// Function to capitalise a string (in place)
// Note - string is not null terminated - we need the length also.
char* capitalise(char* buffer, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        buffer[i] = (char)toupper((int)buffer[i]);
    }
    return buffer;
}

struct Client;

// State of a game
typedef struct Game {
    bool assigned;
    bool inProgress;
    // 0th is white player, 1th is black
    struct Client* players[2];
    // 0 if white, 1 if black
    uint8_t turn;
    // FEN board state
    char* fenBoardState;
} Game;

// State of a client
typedef struct Client {
    // True if this struct corresponds to an actual connected client
    bool assigned;
    // Game playing, NULL if not playing
    Game* game;
    // FEN board state if game finished, or NULL if currently playing
    char* lastGameFen;
    // Desired colour if not playing, current colour if playing
    Colour colour;
    // Priority in queue, lower number = connected first
    long priority;
    bool waitingForHuman;
    FILE* toClientStream;
    FILE* fromClientStream;
} Client;

typedef struct Resources {
    Game* games;
    Client* clients;
    sem_t* dataSemaphore;
    FILE* toEngineStream;
    FILE* fromEngineStream;
} Resources;

// Data passed into client-managing thread function
typedef struct ThreadData {
    // Socket fd from accepted connection
    int acceptedSocketFd;
    Resources* resources;
} ThreadData;

// Ways a game can end
typedef enum GameResult { RESIGNATION, CHECKMATE, STALEMATE } GameResult;

/**
 * @brief Close a client's comms streams, deassign them (so now a new client can
 * take this space in the array)
 *
 * @param client client to remove
 */
void remove_client(Client* client)
{
    fclose(client->toClientStream);
    fclose(client->fromClientStream);
    client->assigned = false;
}

/**
 * @brief Get the name of a game result (checkmate, resignation, stalemate)
 *
 * @param dest write the name here
 * @param result how the game ended
 */
void get_result_name(char* dest, GameResult result)
{
    switch (result) {
    case RESIGNATION:
        strcpy(dest, "resignation");
        break;
    case CHECKMATE:
        strcpy(dest, "checkmate");
        break;
    case STALEMATE:
        strcpy(dest, "stalemate");
        break;
    default:
        // Shouldn't get here
        break;
    }
}

/**
 * @brief End a game
 *
 * @param game game to end
 * @param client losing client (resigner or loser) or null for stalemate
 * @param result how the game ended
 */
void end_game(Game* game, Client* client, GameResult result)
{
    // Find winner
    Colour winningColour = COLOUR_UNSPECIFIED;
    for (int i = 0; i < numPlayers; i++) {
        if (game->players[i] == client) {
            winningColour = (Colour)(!i);
            break;
        }
    }
    // winning colour shouldn't still be UNSPECIFIED
    char winnerName[smallerBufferSize];
    if (result == STALEMATE) {
        strcpy(winnerName, "");
    } else if (winningColour == COLOUR_WHITE) {
        strcpy(winnerName, " white");
    } else {
        strcpy(winnerName, " black");
    }

    // Get the "how" part
    char howEnded[smallerBufferSize];
    get_result_name(howEnded, result);

    // Send game over msg
    char gameOverMsg[smallerBufferSize];
    snprintf(gameOverMsg, smallerBufferSize, "gameover %s%s\n", howEnded,
            winnerName);
    for (int i = 0; i < numPlayers; i++) {
        Client* player = game->players[i];
        if (!player) {
            continue; // no msg to computer
        }
        if (player->lastGameFen) {
            free(player->lastGameFen);
        }
        player->lastGameFen = strdup(game->fenBoardState);
        player->game = NULL;
        if (try_to_write(player->toClientStream, gameOverMsg) == -1) {
            remove_client(player);
            // No need to end the game as well, it is over alr
        }
    }

    game->inProgress = false;
}

/**
 * @brief Reap engine child process, print engine failure msg
 *
 */
void engine_failure(void)
{
    // todo proper message from server, messages to clients
    wait(NULL);
    warn_bug((char*)"engine failure\n");
}

/**
 * @brief SIGPIPE handler for engine failure
 *
 * @param errorCode unused, signal error code (but only SIGPIPE is handled)
 */
void engine_failure_handler(int errorCode __attribute__((unused)))
{
    engine_failure();
}

/**
 * @brief Start new game in Stockfish
 *
 * @param resources shared thread resources
 */
void engine_new_game(Resources* resources)
{
    if (try_to_write(resources->toEngineStream, (char*)"ucinewgame\nisready\n")
            == -1) {
        engine_failure();
    }
    char buffer[maxBufferSize];
    char* readResult
            = fgets(buffer, maxBufferSize, resources->fromEngineStream);
    if (readResult == NULL || strcmp(buffer, "readyok\n") != 0) {
        engine_failure();
    }
}

/**
 * @brief Set position in stockfish without making move
 *
 * @param fen fen string for position setting
 * @param resources shared thread resources
 */
void set_position_no_move(char* fen, Resources* resources)
{
    engine_new_game(resources);
    char posCmd[maxBufferSize];
    snprintf(posCmd, maxBufferSize, "position fen %s\n", fen);
    if (try_to_write(resources->toEngineStream, posCmd) == -1) {
        engine_failure();
    }
}

/**
 * @brief Get best move from engine
 *
 * @param dest write the best move here
 * @param game game to get move for
 * @param resources shared thread resources
 */
void best_move(char* dest, Game* game, Resources* resources)
{
    set_position_no_move(game->fenBoardState, resources);
    if (try_to_write(
                resources->toEngineStream, (char*)"go movetime 500 depth 15\n")
            == -1) {
        engine_failure();
    }
    ChessMoves* move
            = read_stockfish_bestmove_output(resources->fromEngineStream);
    if (move == NULL) {
        engine_failure();
    }
    snprintf(dest, maxBufferSize, "%s", move->moves[0]);
    free_chess_moves(move);
}

/**
 * @brief Check if game against computer (check non-null players)
 *
 * @param game game to check players for
 * @return true if against computer
 * @return false if human vs human
 */
bool game_is_against_computer(Game* game)
{
    if (game->players[0] == NULL && game->players[1] == NULL) {
        warn_bug((char*)"no human players found\n");
    }
    if (game->players[0] != NULL && game->players[1] != NULL) {
        // Both players are humans
        return false;
    }
    return true;
}

void computer_move(Game* game, Resources* resources);

/**
 * @brief Write to client stream, end game if they disconnected
 *
 * @param client client to write to
 * @param msg msg to write
 * @return -1 if game ended, 0 otherwise
 */
int write_to_client(Client* client, char* msg)
{
    if (try_to_write(client->toClientStream, msg) == -1) {
        end_game(client->game, client, RESIGNATION);
        remove_client(client);
        return -1;
    }
    return 0;
}

/**
 * @brief Act on an accepted move
 *
 * @param move move to make in alphanumeric notation
 * @param game game where move was made
 * @param movingClient client making move
 * @param opponent move's opponent
 * @param resources shared client resources
 * @param engineGameState game state as read from Stockfish d output
 */
void move_accepted(char* move, Game* game, Client* movingClient,
        Client* opponent, Resources* resources,
        StockfishGameState* engineGameState)
{
    free(game->fenBoardState);
    game->fenBoardState = strdup(engineGameState->fenString);
    if (movingClient != NULL) {
        if (write_to_client(movingClient, (char*)"ok\n") == -1) {
            return;
        }
    }
    char movedMsg[maxBufferSize];
    snprintf(movedMsg, maxBufferSize, "moved %s\n", move);
    if (opponent != NULL) {
        if (write_to_client(opponent, movedMsg) == -1) {
            return;
        }
    }
    // checkmate, stalemate
    bool inCheck = (engineGameState->checkers != NULL);
    if (try_to_write(resources->toEngineStream, (char*)goPerft1) == -1) {
        engine_failure();
    }
    ChessMoves* nextMoves
            = read_stockfish_go_perft_1_output(resources->fromEngineStream);
    if (!nextMoves) {
        engine_failure();
    }
    if (nextMoves->numMoves == 0) {
        if (inCheck) {
            end_game(game, opponent, CHECKMATE);
        } else {
            end_game(game, NULL, STALEMATE);
        }
    } else if (inCheck) {
        for (int i = 0; i < numPlayers; i++) {
            if (game->players[i]) {
                write_to_client(game->players[i], (char*)"check\n");
            }
        }
    }
    free_chess_moves(nextMoves);
    // assuming game isn't over yet
    game->turn = !(game->turn);
    if (movingClient != NULL && game_is_against_computer(game)
            && game->inProgress) {
        computer_move(game, resources);
    }
}

/**
 * @brief Make a move in a game
 *
 * @param game game to move in
 * @param resources shared thread resources
 * @param move move to make, alphanumeric
 */
void make_move(Game* game, Resources* resources, char* move)
{
    engine_new_game(resources);
    char engineCmd[maxBufferSize];
    snprintf(engineCmd, maxBufferSize, "position fen %s moves %s\nd\n",
            game->fenBoardState, move);
    if (try_to_write(resources->toEngineStream, engineCmd) == -1) {
        engine_failure();
    }
    StockfishGameState* engineGameState
            = read_stockfish_d_output(resources->fromEngineStream);
    if (!engineGameState) {
        engine_failure();
    }
    bool accepted = strcmp(engineGameState->fenString, game->fenBoardState);
    Client* movingClient = game->players[game->turn];
    Client* opponent = game->players[!(game->turn)];
    if (accepted) {
        move_accepted(
                move, game, movingClient, opponent, resources, engineGameState);
    } else {
        // try to send move error to human player
        if (movingClient != NULL) {
            write_to_client(movingClient, (char*)"error move\n");
        }
    }
    free_stockfish_game_state(engineGameState);
}

/**
 * @brief Make computer's move (use engine best move)
 *
 * @param game game to move in
 * @param resources shared thread resources
 */
void computer_move(Game* game, Resources* resources)
{
    if (!(game->players[game->turn] == NULL
                && game->players[!(game->turn)] != NULL)) {
        warn_bug(
                (char*)("tried to make computer move with invalid computer\n"));
    }
    char bestMove[maxBufferSize];
    best_move(bestMove, game, resources);
    make_move(game, resources, bestMove);
}

/**
 * @brief Send started msg to a client
 *
 * @param colour colour the client is starting as
 * @param client client to send to
 */
void send_started(Colour colour, Client* client)
{
    char startedMsg[maxBufferSize];
    char colourName[maxBufferSize];
    get_colour_name(colourName, colour);
    snprintf(startedMsg, maxBufferSize, "started %s\n", colourName);
    write_to_client(client, startedMsg);
}

/**
 * @brief Respond to client "board" cmd
 *
 * @param client client asking for board
 * @param resources engine and client/game array resources
 * @return -1 if error (no board to show), 0 otherwise
 */
int respond_board(Client* client, Resources* resources)
{
    char fen[maxBufferSize];
    bool fenFound = true;
    if (client->lastGameFen) {
        strcpy(fen, client->lastGameFen);
    } else if (client->game && client->game->inProgress) {
        strcpy(fen, client->game->fenBoardState);
    } else {
        fenFound = false;
    }
    if (fenFound) {
        set_position_no_move(fen, resources);
        if (try_to_write(resources->toEngineStream, (char*)"d\n") == -1) {
            engine_failure();
        }
        StockfishGameState* gameState
                = read_stockfish_d_output(resources->fromEngineStream);
        if (!gameState) {
            engine_failure();
        }
        char boardMsg[maxBufferSize];
        snprintf(boardMsg, maxBufferSize, "startboard\n%sendboard\n",
                gameState->boardString);
        write_to_client(client, boardMsg);
        free_stockfish_game_state(gameState);
        return 0;
    }
    return -1;
}

/**
 * @brief check if colours can play against each other
 *
 * @param colour1 colour of one player
 * @param colour2 colour of another player
 * @return true if colours can play against each other, false if not
 */
bool colours_can_play(Colour colour1, Colour colour2)
{
    return colour1 == COLOUR_UNSPECIFIED || colour2 == COLOUR_UNSPECIFIED
            || (colour1 != colour2);
}

/**
 * @brief Find first unassigned game in array, does not assign it
 *
 * @param resources shared thread resources
 * @return first unassigned game
 */
Game* get_unassigned_game(Resources* resources)
{
    Game* game = resources->games;
    bool found = false;
    for (long i = 0; i < maxBufferSize; i++) {
        if (!(*game).assigned) {
            found = true;
            break;
        }
        game++;
    }
    if (!found) {
        warn_bug((char*)"Ran out of space in game array\n");
    }
    return game;
}

/**
 * @brief Initialise a new game
 *
 * @param game game to initialise
 */
void initialise_game(Game* game)
{
    game->assigned = true;
    game->turn = COLOUR_WHITE;
    game->inProgress = false;
    game->fenBoardState = strdup("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/"
                                 "RNBQKBNR w KQkq - 0 1");
    game->inProgress = true;
}

/**
 * @brief Try to match human with another
 *
 * @param human human to match
 * @param resources shared thread resources
 * @param colour colour chosen by human
 */
void try_to_match_human(Client* human, Resources* resources)
{
    // Find human who joined first
    human->waitingForHuman = true;
    bool found = false;
    Client* otherHuman = resources->clients;
    Client* humanFirstJoined;
    long minPriority
            = otherHuman->priority + 1; // will be set on first iteration
    for (long i = 0; i < maxBufferSize; i++) {
        if (otherHuman->assigned && otherHuman->waitingForHuman
                && colours_can_play(human->colour, otherHuman->colour)
                && otherHuman->priority < minPriority && otherHuman != human) {
            found = true;
            humanFirstJoined = otherHuman;
            minPriority = otherHuman->priority;
        }
        otherHuman++;
    }
    if (!found) {
        return; // No match, do nothing for now
    }

    // Set colours of humans
    otherHuman = humanFirstJoined;
    if (human->colour != COLOUR_UNSPECIFIED) {
        otherHuman->colour = (Colour)(!(human->colour));
    } else if (otherHuman->colour != COLOUR_UNSPECIFIED) {
        human->colour = (Colour)(!(otherHuman->colour));
    } else {
        // This human is white iff they joined first
        Colour humanColour
                = (human->priority < otherHuman->priority ? COLOUR_WHITE
                                                          : COLOUR_BLACK);
        human->colour = humanColour;
        otherHuman->colour = (Colour)(!humanColour);
    }

    // Initialise players/game, send started msg to both
    Game* game = get_unassigned_game(resources);
    initialise_game(game);
    game->players[human->colour] = human;
    game->players[!(human->colour)] = otherHuman;
    for (int i = 0; i < numPlayers; i++) {
        Client* player = game->players[i];
        player->game = game;
        player->waitingForHuman = false;
        send_started(player->colour, player);
    }
}

/**
 * @brief Respond to start message from client
 *
 * @param client client sending start msg
 * @param resources shared array/engine resources
 * @param fields fields from input line
 * @return true if command valid, false otherwise
 */
bool respond_start(Client* client, Resources* resources, char** fields)
{
    Opponent opponent;
    Colour colour;
    if (!strcmp(fields[1], "computer")) {
        opponent = OPPONENT_COM;
    } else if (!strcmp(fields[1], "human")) {
        opponent = OPPONENT_HUMAN;
    } else {
        return false;
    }
    if (!strcmp(fields[2], "white")) {
        colour = COLOUR_WHITE;
    } else if (!strcmp(fields[2], "black")) {
        colour = COLOUR_BLACK;
    } else if (!strcmp(fields[2], "either")) {
        colour = COLOUR_UNSPECIFIED;
    } else {
        return false;
    }

    // Start new game
    if (client->game != NULL) {
        end_game(client->game, client, RESIGNATION);
    }
    if (opponent == OPPONENT_COM && colour == COLOUR_UNSPECIFIED) {
        colour = COLOUR_WHITE;
    }
    Game* game;
    client->lastGameFen = NULL;
    client->colour = colour;
    switch (opponent) {
    case OPPONENT_COM:
        game = get_unassigned_game(resources);
        initialise_game(game);
        game->players[colour] = client;
        game->players[!colour] = NULL; // computer
        client->game = game;
        client->waitingForHuman = false;
        send_started(colour, client);
        if (colour == COLOUR_BLACK) {
            // Human is black, computer starts off as white
            computer_move(game, resources);
        }
        return true;
    case OPPONENT_HUMAN:
        try_to_match_human(client, resources);
        return true;
    default:
        return true; // shouldn't get here
    }
}

/**
 * @brief Respond to client "hint" msg, assume it is their turn and they are
 * playing and cmd is valid
 *
 * @param client client that asked for hint
 * @param resources shared thread resources
 * @param all true if all, false if best
 */
void respond_hint(Client* client, Resources* resources, bool all)
{
    if (all) {
        set_position_no_move(client->game->fenBoardState, resources);
        if (try_to_write(resources->toEngineStream, (char*)goPerft1) == -1) {
            engine_failure();
        }
        ChessMoves* moves
                = read_stockfish_go_perft_1_output(resources->fromEngineStream);
        write_to_client(client, (char*)"moves");
        for (long i = 0; i < moves->numMoves; i++) {
            char allMovesMsg[maxBufferSize];
            snprintf(allMovesMsg, maxBufferSize, " %s", moves->moves[i]);
            write_to_client(client, allMovesMsg);
        }
        write_to_client(client, (char*)"\n");
        free_chess_moves(moves);
    } else {
        char bestMove[smallerBufferSize];
        best_move(bestMove, client->game, resources);
        char bestMoveMsg[smallerBufferSize];
        snprintf(bestMoveMsg, smallerBufferSize, "moves %s\n", bestMove);
        write_to_client(client, bestMoveMsg);
    }
}

/**
 * @brief Resign a client's game if they have one and remove the client from the
 * array
 *
 * @param client
 */
void resign_remove_client(Client* client)
{
    if (client->game && client->game->inProgress) {
        // If client disconnects before getting a game, client->game is NULL, no
        // need to end it.
        end_game(client->game, client, RESIGNATION);
    }
    remove_client(client);
}

/**
 * @brief Respond to two-word client input
 *
 * @param cmd first word of input
 * @param fields fields from input line
 * @param client client sending input
 * @param resources shared thread resources
 * @return errorCommand, errorGame or errorTurn to indicate error, 0 for no
 * error
 */
int respond_medium_input(
        char* cmd, char** fields, Client* client, Resources* resources)
{
    if (!strcmp(cmd, "move")) {
        if (!(valid_move_length(strlen(fields[1]))
                    && str_is_alnum(fields[1]))) {
            return errorCommand;
        }
        if (!(client->game)) {
            return errorGame;
        }
        if (client->colour != client->game->turn) {
            return errorTurn;
        }
        make_move(client->game, resources, fields[1]);
        return 0;
    }
    if (!strcmp(cmd, "hint")) {
        bool all;
        if (!strcmp(fields[1], "all")) {
            all = true;
        } else if (!strcmp(fields[1], "best")) {
            all = false;
        } else {
            return errorCommand;
        }
        if (!(client->game)) {
            return errorGame;
        }
        if (client->colour != client->game->turn) {
            return errorTurn;
        }
        respond_hint(client, resources, all);
        return 0;
    }
    return errorCommand;
}

/**
 * @brief Respond to one word input from client
 *
 * @param client client giving input
 * @param resources shared thread resources
 * @param cmd first (and only) word of input
 * @return errorCommand, errorMove, errorTurn or 0 for no error
 */
int respond_short_input(Client* client, Resources* resources, char* cmd)
{
    if (!strcmp(cmd, "board")) {
        if (respond_board(client, resources) == -1) {
            return errorGame;
        }
        return 0;
    }
    if (!strcmp(cmd, "resign")) {
        if (!(client->game)) {
            return errorGame;
        }
        end_game(client->game, client, RESIGNATION);
        return 0;
    }
    return errorCommand;
}

/**
 * @brief Repeatedly act on commands from client
 *
 * @param client ptr to cliend data
 * @param resources shared engine/data resources
 */
void client_loop(Client* client, Resources* resources)
{
    char buffer[maxBufferSize];
    char* readResult;
    while (readResult = fgets(buffer, maxBufferSize, client->fromClientStream),
            readResult != NULL) {
        sem_wait(resources->dataSemaphore);
        if (feof(client->fromClientStream)) {
            // client closed, ignore partial command entered
            sem_post(resources->dataSemaphore);
            break;
        }
        int error = 0;
        if (validate_line(buffer) == -1) {
            error = errorCommand;
        }
        char** fields = split_by_char(buffer, ' ', 0);
        char* cmd = fields[0];
        int numFields = count_fields(fields);
        if (!error) {
            if (numFields == shortLine) {
                error = respond_short_input(client, resources, cmd);
            } else if (numFields == mediumLine) {
                error = respond_medium_input(cmd, fields, client, resources);
            } else if (numFields == longLine) {
                if (!strcmp(cmd, "start")) {
                    error = respond_start(client, resources, fields)
                            ? 0
                            : errorCommand;
                } else {
                    error = errorCommand;
                }
            } else {
                error = errorCommand;
            }
        }
        free(fields);
        char* response = NULL;
        if (error == errorCommand) {
            response = (char*)"error command\n";
        } else if (error == errorGame) {
            response = (char*)"error game\n";
        } else if (error == errorTurn) {
            response = (char*)"error turn\n";
        }
        if (response) {
            write_to_client(client, response);
        }
        sem_post(resources->dataSemaphore);
    }
    resign_remove_client(client);
}

/**
 * @brief thread function to manage a client
 *
 * @param dataIn ptr to ThreadData, contains data supplied to thread
 * @return NULL
 */
void* client_thread(void* dataIn)
{
    ThreadData threadData = *(ThreadData*)dataIn;
    Resources* resources = threadData.resources;
    free(dataIn);

    sem_wait(resources->dataSemaphore);

    Client* client = resources->clients;
    Client* thisClient;
    bool found = false;
    long maxPriority = -1;
    for (long i = 0; i < maxBufferSize; i++) {
        if (!(*client).assigned) {
            if (!found) {
                thisClient = client;
                found = true;
            }
        } else {
            if ((*client).priority > maxPriority) {
                maxPriority = (*client).priority;
            }
        }

        client++;
    }
    if (!found) {
        warn_bug((char*)"Ran out of space in client array\n");
    }

    thisClient->assigned = true;
    thisClient->game = NULL; // not playing yet
    thisClient->lastGameFen = NULL;
    thisClient->fromClientStream = fdopen(threadData.acceptedSocketFd, "r");
    thisClient->toClientStream = fdopen(threadData.acceptedSocketFd, "w");
    thisClient->waitingForHuman = false;
    if (maxPriority == -1) {
        thisClient->priority = 1;
    } else {
        thisClient->priority = maxPriority + 1;
    }

    sem_post(resources->dataSemaphore);

    client_loop(thisClient, resources);

    // Ending thread
    // Client streams closed in remove_client
    return NULL; // could have called pthread_exit(NULL);
}

/**
 * @brief Set up signal handler to ignore SIGPIPE
 */
void ignore_sig_pipe(void)
{
    struct sigaction sigPipeAction;
    memset(&sigPipeAction, 0, sizeof(sigPipeAction));
    sigPipeAction.sa_handler = engine_failure_handler;
    sigPipeAction.sa_flags = SA_RESTART;
    sigaction(SIGPIPE, &sigPipeAction, NULL);
}

/**
 * @brief Enter loop of accepting connections
 *
 * @param fdServer server socket fd
 * @param toEngineStream stream to write to client
 * @param fromEngineStream stream to write from client
 */
void process_connections(
        int fdServer, FILE* toEngineStream, FILE* fromEngineStream)
{
    int fd;
    struct sockaddr_in fromAddr;
    socklen_t fromAddrSize;

    // Initialise semaphores
    sem_t* dataSemaphore = (sem_t*)malloc(sizeof(sem_t));
    sem_init(dataSemaphore, 0, 1);

    // Initialise resources
    Resources* resources = (Resources*)malloc(sizeof(Resources));
    resources->dataSemaphore = dataSemaphore;
    resources->clients = (Client*)calloc(maxBufferSize, sizeof(Client));
    resources->games = (Game*)calloc(maxBufferSize, sizeof(Game));
    for (long i = 0; i < maxBufferSize; i++) {
        resources->clients[i].assigned = false;
        resources->games->assigned = false;
    }
    resources->toEngineStream = toEngineStream;
    resources->fromEngineStream = fromEngineStream;
    ignore_sig_pipe();

    // Repeatedly accept connections
    while (1) {
        fromAddrSize = sizeof(struct sockaddr_in);
        // Block, waiting for a new connection. (fromAddr will be populated
        // with address of client)
        fd = accept(fdServer, (struct sockaddr*)&fromAddr, &fromAddrSize);
        if (fd < 0) {
            // Error accepting connection - just skip
            continue;
        }

        // Create a thread to deal with client
        ThreadData* threadData = (ThreadData*)malloc(sizeof(ThreadData));
        threadData->acceptedSocketFd = fd;
        threadData->resources = resources;
        pthread_t threadID;
        pthread_create(&threadID, NULL, client_thread, threadData);
        pthread_detach(threadID);
    }

    sem_destroy(dataSemaphore);
    free(dataSemaphore);
}

/**
 * @brief Print can't start listening and exit with code cantStartCommsExitCode.
 */
void warn_cant_start_comms(void)
{
    fprintf(stderr,
            "uqchessserver: cannot start communication with chess engine\n");
    fflush(stderr);
    exit(cantStartCommsExitCode);
}

/**
 * @brief Send msg to engine, wait for response. Reap and exit upon failure
 *
 * @param msg msg to engine, don't add newline
 * @param response msg expected from engine, don't add newline
 * @param toEngineStream stream to engine's stdin
 * @param fromEngineStream stream from engine's stdout
 */
void send_wait(
        char* msg, char* response, FILE* toEngineStream, FILE* fromEngineStream)
{
    if (fprintf(toEngineStream, "%s\n", msg) < 0
            || fflush(toEngineStream) == EOF) {
        wait(NULL);
        warn_cant_start_comms();
    }
    char buffer[maxBufferSize];
    char* readResult;
    while (1) {
        readResult = fgets(buffer, maxBufferSize, fromEngineStream);
        if (readResult == NULL || remove_newline(buffer) == -1) {
            // EOF reading from engine
            wait(NULL);
            warn_cant_start_comms();
        }
        if (!strcmp(buffer, response)) {
            break;
        }
    }
}

/**
 * @brief Starts engine (stockfish), get r/w streams
 *
 * @param toEngineStream put the write to engine stream here
 * @param fromEngineStream put the read from engine stream here
 */
void start_engine(FILE** toEngineStream, FILE** fromEngineStream)
{
    int serverToEnginePipe[2];
    int engineToServerPipe[2];
    pipe(serverToEnginePipe);
    pipe(engineToServerPipe);
    int childId = fork();
    if (!childId) {
        // Child - engine
        close(serverToEnginePipe[1]);
        close(engineToServerPipe[0]);
        dup2(serverToEnginePipe[0], STDIN_FILENO);
        close(serverToEnginePipe[0]);
        dup2(engineToServerPipe[1], STDOUT_FILENO);
        close(engineToServerPipe[1]);
        execlp("stockfish", "stockfish", NULL);
    }
    // Parent - server
    close(serverToEnginePipe[0]);
    close(engineToServerPipe[1]);
    *toEngineStream = fdopen(serverToEnginePipe[1], "w");
    *fromEngineStream = fdopen(engineToServerPipe[0], "r");
    send_wait((char*)"isready", (char*)"readyok", *toEngineStream,
            *fromEngineStream);
    send_wait((char*)"uci", (char*)"uciok", *toEngineStream, *fromEngineStream);
}

int main(int argc, char* argv[])
{
    Args args = get_args(argc, argv);
    int listenFd;
    uint16_t portNum;
    if (open_listen(args.portFromCmdLine, &listenFd, &portNum) == -1) {
        warn_cant_start_listening(args.portFromCmdLine);
    }

    FILE* toEngineStream = NULL;
    FILE* fromEngineStream = NULL;
    start_engine(&toEngineStream, &fromEngineStream);

    fprintf(stderr, "%u\n", portNum);
    fflush(stderr);

    process_connections(listenFd, toEngineStream, fromEngineStream);

    return 0;
}