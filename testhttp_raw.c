#include <netdb.h>
#include <zconf.h>
#include <stdlib.h>
#include <stdio.h>
#include "err.h"
#include "string.h"

/* Used for malloc'ing space for some headers and values in the request */
#define HEADERS_SIZE 512

/* Finds size of the cookies to malloc space for the cookies in the request. */
uint64_t getFileSize(FILE *file) {
    fseek(file, 0L, SEEK_END);
    uint64_t size = ftell(file);
    fseek(file, 0L, SEEK_SET);

    return size;
}

/* Appends cookies from file to the request.
 * Format is as follows:
 * Cookie: key1=value1; key2=value2; ... */
void appendCookies(FILE *file, char *message) {
    size_t bufSize;
    char *line;
    ssize_t lineSize = getline(&line, &bufSize, file);

    if (lineSize <= 0)
        return;

    strcat(message, "Cookie: ");
    line[strlen(line) - 1] = '\0';
    strcat(message, line);

    while (getline(&line, &bufSize, file) > 0) {
        strcat(message, "; ");
        line[strlen(line) - 1] = '\0';
        strcat(message, line);
    }
    strcat(message, "\r\n");
}

/* Creates the request to be sent to a server. */
char *makeRequest(char *fileName, char *testedPage, char *host) {
    FILE *cookieFile;
    cookieFile = fopen(fileName, "r");

    if (!cookieFile)
        fatal("File with cookies cannot be opened!");

    uint64_t fileSize = getFileSize(cookieFile);

    /* HTTP Request starter */
    char *message = malloc((HEADERS_SIZE + fileSize) * sizeof(char));
    strcat(message, "GET ");
    strcat(message, testedPage);
    strcat(message, " HTTP/1.1\r\n");

    /* Host in the request */
    strcat(message, "Host: ");
    strcat(message, host);
    strcat(message, "\r\n");

    /* Append the cookies */
    appendCookies(cookieFile, message);

    strcat(message, "Connection: close\r\n");
    strcat(message, "\r\n");
    fclose(cookieFile);

    return message;
}

/* Checks if status in response message is "200 OK".
 * In case it is smth else, sets isOk to 0(false),
 * Otherwise sets isOk to 1(true)*/
char *checkStatus(char *lineBuf, int *isOk) {
    strtok(lineBuf, " ");
    char *status = strtok(NULL, "\r");
    char *okStatus = "200 OK";

    if (strcmp(status, okStatus) == 0) {
        *isOk = 1;
        return status;
    }

    *isOk = 0;
    return status;
}

/* Checks if header is:
 * Set-Cookie, then prints the key=value
 * Transfer-Encoding, then checks if it is chunked.
 * In case it is not chunked but smth else, shows ERROR.
 * In case it is chunked, sets isChunked to 1(true).*/
void cookieOrEncoding(char *lineBuf, int *isChunked) {
    const char *setCookie = "Set-Cookie:";
    const char *encoding = "Transfer-Encoding:";

    /* E.g. Transfer-Encoding: chunked */
    const char *header = strtok(lineBuf, " ");
    if (strcmp(header, setCookie) == 0) {
        char *cookie = strtok(NULL, "\n");
        /* lineBuf = "key=value" */
        const char *value = strtok(cookie, ";\n");
        fprintf(stdout, "%s\n", value);
    } else if (strcmp(header, encoding) == 0) {
        const char *value = strtok(NULL, "\n");
        /* lineBuf = "chunked" */
        if (strcmp(value, "chunked\r") != 0) {
            syserr("Only chunked is served");
        }
        *isChunked = 1;
    }
}

/* Reads data that is chunked. Calculates the sum of each chunked data.
 * Returns the overall length of all chunks */
uint64_t readChunkedLength(FILE *socketFile) {
    char *lineBuf = NULL;
    size_t lineBufSize = 0;
    uint64_t result = 0;

    while (getline(&lineBuf, &lineBufSize, socketFile) >= 0) {
        char *numberStr = strtok(lineBuf, "\r");
        int dataSize;
        sscanf(numberStr, "%x", &dataSize);

        if (dataSize == 0) {
            break;
        }

        size_t size = dataSize + 2;
        char *buf = malloc(size * sizeof(char));
        if (fread(buf, size, 1, socketFile) < 1) {
            syserr("fread failed");
        }

        result += dataSize;
        free(buf);
    }

    return result;
}

/* Returns the length of data that is not chunked. */
uint64_t readUsualLength(FILE *socketFile) {
    uint64_t result = 0;
    ssize_t lineSize;
    char *lineBuf = NULL;
    size_t lineBufSize = 0;

    while ((lineSize = getline(&lineBuf, &lineBufSize, socketFile)) >= 0) {
        result += lineSize;
    }

    return result;
}

int main(int argc, char *argv[]) {
    int sock;
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;

    if (argc != 4) {
        fatal("Usage: %s host:port cookies webPage...\n", argv[0]);
    }

    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;

    /* Separate Host and Port in argv[1] */
    char *firstArg = argv[1];
    char *host = strtok(firstArg, ":");
    char *portStr = strtok(NULL, " ");

    int err = getaddrinfo(host, portStr, &addr_hints, &addr_result);

    if (err == EAI_SYSTEM) { // system error
        syserr("getaddrinfo: %s", gai_strerror(err));
    } else if (err != 0) { // other error (host not found, etc.)
        fatal("getaddrinfo: %s", gai_strerror(err));
    }

    // Initialize socket according to getaddrinfo results
    sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (sock < 0)
        syserr("socket");

    // Connect socket to the server
    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) != 0)
        syserr("connect");

    freeaddrinfo(addr_result);

    /* Making of HTTP request */
    char *message = makeRequest(argv[2], argv[3], host);

    /* Sending of the request */
    unsigned messageSize = strlen(message);
    if (write(sock, message, messageSize) != messageSize) {
        syserr("partial / failed write");
    }

    /* Reading, parsing and output of the response */
    FILE *socketFile;
    socketFile = (FILE *) fdopen(sock, "r");

    char *lineBuf = NULL;
    size_t lineBufSize = 0;
    ssize_t lineSize;

    lineSize = getline(&lineBuf, &lineBufSize, socketFile);
    if (lineSize < 0)
        syserr("status");

    /* Checks if status is 200 OK or smth else. */
    int isOk;
    char *status = checkStatus(lineBuf, &isOk);

    int isChunked = 0;

    if (isOk == 1) {
        /* In case of error or EOF lineSize = -1 */
        while (lineSize >= 0) {
            if (lineSize == 2 && strcmp(lineBuf, "\r\n") == 0) {
                break;
            }

            lineSize = getline(&lineBuf, &lineBufSize, socketFile);
            cookieOrEncoding(lineBuf, &isChunked);
        }
    } else {
        fprintf(stdout, "%s\n", status);
    }

    if (isOk == 1) {
        uint64_t lengthOfMessage;
        if (isChunked) {
            /* Message should be read as chunked */
            lengthOfMessage = readChunkedLength(socketFile);
        } else {
            /* Message is usual without encoding */
            lengthOfMessage = readUsualLength(socketFile);
        }

        fprintf(stdout, "Dlugosc zasobu: %lu\n", lengthOfMessage);
    }

    free(lineBuf);
    lineBuf = NULL;

    /* Close the socket file */
    fclose(socketFile);

    /* Close the socket */
    (void) close(sock); // socket would be closed anyway when the program ends

    return 0;
}