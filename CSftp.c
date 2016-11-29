#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "dir.h"
#include "usage.h"

#define DEBUG 1

#define BACKLOG 10
#define FTP_MAX_LEN 256


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void ssend(int sockfd, char* buf) {
    if (send(sockfd, buf, strlen(buf), 0) < 0) {
        perror("error on send");
        // TODO: exit?
    } else {
        #if DEBUG
        printf("--> %s\n", buf);
        #endif
    }
}

void srecv(int sockfd, char* buf) {
    int in_len;
    bzero(buf, FTP_MAX_LEN);
    if ((in_len = recv(sockfd, buf, FTP_MAX_LEN, 0)) < 0) {
        perror("error reading from socket");
        // TODO: exit?
    } else {
        //buf[in_len] = '\0';
        #if DEBUG
        printf("<-- %s\n", buf);
        #endif
    }
}

void sendStatus(int sockfd, int status) {
    char* str;
    switch(status) {
        case 200:
            str = "200 Command okay.\n";
            break;
        case 220:
            str = "220 Service ready for new user.\n";
            break;
        case 230:
            str = "230 User logged in, proceed.\n";
            break;
        case 500:
            str = "500 Syntax error, command unrecognized.\n";
            break;
        case 530:
            str = "530 Not logged in.\n";
            break;
    }
    ssend(sockfd, str);
}

void stringToUpper(char *s) {
    int i;
    for(i = 0; s[i] != '\0'; s++) {
        s[i] = toupper(s[i]);
    }
}

// Here is an example of how to use the above function. It also shows
// one how to get the arguments passed on the command line.

int main(int argc, char **argv) {

    // This is some sample code feel free to delete it
    // This is the main program for the thread version of nc

    int i;
    
    // Check the command line arguments
    if (argc != 2) {
      usage(argv[0]);
      return -1;
    }

    int port = atoi(argv[1]);


    int sockfd, newsockfd;
    struct sockaddr_in serv_addr;
    struct sockaddr_storage client_addr;
    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];
    char buffer[FTP_MAX_LEN];

    // keep state of ftp connection
    int userLoggedIn = 0;

    // create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("error opening socket");
        exit(1);
    }

    memset((char*) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    // bind host address
    if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("error binding");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) < 0) {
        perror("error on listen");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while(1) {
        sin_size = sizeof(client_addr);
        newsockfd = accept(sockfd, (struct sockaddr*)&client_addr, &sin_size);
        if (newsockfd == -1) {
            perror("error on accept");
            continue;
        }

        inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr*)&client_addr), s, sizeof(s));
        printf("server: got connection from %s\n", s);

        sendStatus(newsockfd, 220);
        while(1) {
            srecv(newsockfd, buffer);
            
            // More general pattern:
            char* str = strdup(buffer);  // We own str's memory now.
            char* cmd = strsep(&str, " ");
            stringToUpper(cmd);

            char* params;

            if (strncmp("USER", cmd, 4) == 0) {
                if (userLoggedIn) {
                    sendStatus(newsockfd, 500);
                } else {
                    params = strsep(&str, " ");
                    params[strcspn(params, "\r\n")] = 0; 

                    #if DEBUG
                    printf("server: USER command, params: %s\n", params);
                    printf("strcmp: %d, len: %d\n", strcmp(params, "cs317"), strlen(params));
                    #endif

                    if (strcmp("cs317", params) == 0) {
                        sendStatus(newsockfd, 230);
                        userLoggedIn = 1;
                    } else {
                        sendStatus(newsockfd, 530);
                    }
                }
            } else if (strncmp("QUIT", cmd, 4) == 0) {

            } else if (strncmp("TYPE", cmd, 4) == 0) {
                params = strsep(&str, " ");
                params[strcspn(params, "\r\n")] = 0; 
                stringToUpper(params);

                if (strcmp("I", params) == 0 ||
                    strcmp("A", params) == 0) {
                    sendStatus(newsockfd, 200);
                } else {
                    sendStatus(newsockfd, 500);
                }
            } else if (strncmp("MODE", cmd, 4) == 0) {
                params = strsep(&str, " ");
                params[strcspn(params, "\r\n")] = 0; 
                stringToUpper(params);

                if (strcmp("S", params) == 0) {
                    // stream mode, only mode necessary to implement
                    sendStatus(newsockfd, 200);
                } else {
                    sendStatus(newsockfd, 500);
                }
            } else if (strncmp("STRU", cmd, 4) == 0) {
                params = strsep(&str, " ");
                params[strcspn(params, "\r\n")] = 0; 
                stringToUpper(params);

                if (strcmp("F", params) == 0) {
                    // file structure, only mode necessary to implement
                    sendStatus(newsockfd, 200);
                } else {
                    sendStatus(newsockfd, 500);
                }
            } else if (strncmp("RETR", cmd, 4) == 0) {

            } else if (strncmp("PASV", cmd, 4) == 0) {

            } else if (strncmp("NLST", cmd, 4) == 0) {

            } else {
                sendStatus(newsockfd, 500);
            }

            free(str);
        }
    }


    // This is how to call the function in dir.c to get a listing of a directory.
    // It requires a file descriptor, so in your code you would pass in the file descriptor 
    // returned for the ftp server's data connection
    
    printf("Printed %d directory entries\n", listFiles(1, "."));
    return 0;

}
