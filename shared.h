#ifndef SHARED_H
#define SHARED_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

extern int const maxBufferSize;
extern int const smallerBufferSize;

// min/max move string length for a UCI move
extern size_t const maxMoveLen;
extern size_t const minMoveLen;

// Number of fields that can be in valid instruction lines
extern int const shortLine;
extern int const mediumLine;
extern int const longLine;

/**
 * @brief Warn of program bug (e.g. reaching code that should be unreachable)
 * and exit.
 *
 * @param msg warning message.
 */
void warn_bug(char* msg);

/**
 * @brief Return number of non-null elements of fields
 *
 * @param fields array of fields
 * @return number of non-null elements of fields
 */
int count_fields(char** fields);

/**
 * @brief Check if str is entirely alphanumeric
 *
 * @param str string to check (null-terminated)
 * @return true if str is entirely alphanumeric
 * @return false if str has a non-alnum character
 */
bool str_is_alnum(char* str);

/**
 * @brief Check if str starts with -- (could be a command-line argument).
 *
 * @param str string to check.
 * @return true if str starts with --, false otherwise.
 */
bool is_option(char* str);

// Possible opponent types for a game
typedef enum { OPPONENT_COM, OPPONENT_HUMAN, OPPONENT_UNSPECIFIED } Opponent;

// Possible colours to play as for a game
typedef enum { COLOUR_WHITE = 0, COLOUR_BLACK = 1, COLOUR_UNSPECIFIED } Colour;

// Whose turn it is
typedef enum { MY_TURN, THEIR_TURN } Turn;

/**
 * @brief Get socket for given port.
 *
 * @param port service name or port num
 * @return socket fd or -1 if there was a socket/connection error
 */
int get_socket_fd(char* port);

/**
 * @brief Convert colour to colour name ("white", "black", "either"
 * (unspecified))
 *
 * @param dest where to write the result
 * @param colour colour to convert
 */
void get_colour_name(char* dest, Colour colour);

/**
 * @brief Convert opponent type to opponent name ("computer", "human")
 *
 * @param dest where to write the result
 * @param opponent opponent type to convert
 */
void get_opponent_name(char* dest, Opponent opponent);

/**
 * @brief Checks if the given string length is a valid length for a UCI move
 * string (between 4 and 5).
 *
 * @param length length of the move string
 * @return true if 4 <= length <= 5
 * @return false otherwise
 */
bool valid_move_length(size_t length);

/**
 * @brief Checks if the input can be validly split into tokens - at least 2
 * chars long, first and second-last characters aren't a space (accounting for
 * last character being a newline).
 *
 * @param input input received from stdin or socket
 * @return true if input satisfies above condition
 * @return false otherwise
 */
bool has_valid_tokens(char* input);

/**
 * @brief Remove terminating newline from string (replace with null char).
 *
 * @param str string to modify, modified in place
 * @return int 0 if newline was replaced, -1 if no terminating newline found
 */
int remove_newline(char* str);

/**
 * @brief Try to write and flush to stream
 *
 * @param stream stream to write to
 * @param text text to write, newline-terminated
 * @return -1 on error, 0 on success
 */
int try_to_write(FILE* stream, char* text);

/**
 * @brief Ensure line is not blank and ends with newline. Removes newline. Check
 * if it has valid tokens
 *
 * @param line line of input to check
 * @return -1 if invalid tokens, 0 if valid tokens
 */
int validate_line(char* line);

#endif