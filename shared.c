#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <netdb.h>
#include <ctype.h>
#include "shared.h"

// Function/constant comments are in shared.h

int const maxBufferSize = 10000;
int const smallerBufferSize = 100;
size_t const minMoveLen = 4;
size_t const maxMoveLen = 5;

// Number of fields that can be in valid instruction lines
int const shortLine = 1;
int const mediumLine = 2;
int const longLine = 3;

const char white[] = "white";
const char black[] = "black";
const char either[] = "either";
const char computer[] = "computer";
const char human[] = "human";

void warn_bug(char* msg)
{
    fprintf(stderr, "bug: %s", msg);
    fflush(stderr);
    exit(EXIT_FAILURE);
}

bool is_option(char* str)
{
    return !strncmp(str, "--", 2);
}

bool str_is_alnum(char* str)
{
    for (; *str != '\0'; str++) {
        if (!isalnum(*str)) {
            return false;
        }
    }
    return true;
}

int count_fields(char** fields)
{
    int count;
    for (count = 0; fields[count] != NULL; count++) {
    }
    return count;
}

int get_socket_fd(char* port)
{
    // REF: very similar to lecture net2.c
    struct addrinfo* ai = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    int err;
    if ((err = getaddrinfo("localhost", port, &hints, &ai))) {
        // could not work out the address
        freeaddrinfo(ai);
        return -1;
    }
    // create socket. 0 == use default stream protocol (TCP)
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        // error creating socket
        return -1;
    }
    if (connect(fd, ai->ai_addr, sizeof(struct sockaddr))) {
        // Error connecting
        return -1;
    }
    return fd;
}

void get_colour_name(char* dest, Colour colour)
{
    switch (colour) {
    case COLOUR_WHITE:
        snprintf(dest, maxBufferSize, "%s", white);
        break;
    case COLOUR_BLACK:
        snprintf(dest, maxBufferSize, "%s", black);
        break;
    case COLOUR_UNSPECIFIED:
        snprintf(dest, maxBufferSize, "%s", either);
        break;
    }
}

void get_opponent_name(char* dest, Opponent opponent)
{
    switch (opponent) {
    case OPPONENT_COM:
        snprintf(dest, maxBufferSize, "%s", computer);
        break;
    case OPPONENT_HUMAN:
        snprintf(dest, maxBufferSize, "%s", human);
        break;
    default:
        warn_bug((char*)"opponent should be specified");
    }
}

bool valid_move_length(size_t length)
{
    return minMoveLen <= length && length <= maxMoveLen;
}

bool has_valid_tokens(char* input)
{
    return !(strlen(input) < 2 || input[0] == ' '
            || input[strlen(input) - 2] == ' ');
}

int remove_newline(char* str)
{
    int len = strlen(str);
    if (len == 0 || str[len - 1] != '\n') {
        return -1;
    }
    str[len - 1] = '\0';
    return 0;
}

int try_to_write(FILE* stream, char* text)
{
    if (fprintf(stream, "%s", text) < 0 || fflush(stream) == EOF) {
        return -1;
    }
    return 0;
}

int validate_line(char* line)
{
    if (strlen(line) == 0) {
        warn_bug((char*)"this line length code should be unreachable\n");
    }
    if (remove_newline(line) == -1) {
        warn_bug((char*)"Line should end with newline\n");
    }
    if (!has_valid_tokens(line)) {
        // msg is invalid
        return -1;
    }
    return 0;
}