#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include "dir.h"
#include "usage.h"
#include "Thread.h"

#define DEBUG 1

#define BACKLOG 10
#define NUM_THREADS 4
#define FTP_MAX_LEN 512
#define SEND_BUF_LEN 1024


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
    
    // clear buffer before receive
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
        case 125:
            str = "125 Data connection already open; transfer starting.\n";
            break;
        case 200:
            str = "200 Command okay.\n";
            break;
        case 220:
            str = "220 Service ready for new user.\n";
            break;
        case 221:
            str = "221 Service closing control connection.\n";
            break;
        case 226:
            str = "226 Closing data connection.\n";
            break;
        case 230:
            str = "230 User logged in, proceed.\n";
            break;
        case 250:
            str = "250 Requested file action okay, completed.\n";
            break;
        case 500:
            str = "500 Syntax error, command unrecognized.\n";
            break;
        case 530:
            str = "530 Not logged in.\n";
            break;
        case 550:
            str = "550 Requested action not taken.\n";
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

void* mainLoop(void* param) {
    int sockfd = *((int*)param);

    // control connection
    int newsockfd;
    struct sockaddr_storage client_addr;
    socklen_t sin_size;
    char buffer[FTP_MAX_LEN];

    // keep state of ftp connection
    int userLoggedIn = 0;

    // data connection
    int datasockfd, newdatasockfd;
    struct sockaddr_in data_addr;
    struct sockaddr_storage data_client_addr;
    socklen_t data_sin_size;
    int data_port;
    unsigned long data_ip;

    #if DEBUG
    // used for storing string version of IP address on connection
    char data_s[INET6_ADDRSTRLEN];
    #endif

    while(1) {
        sin_size = sizeof(client_addr);
        newsockfd = accept(sockfd, (struct sockaddr*)&client_addr, &sin_size);
        if (newsockfd == -1) {
            perror("error on accept");
            continue;
        }

        #if DEBUG
        char s[INET6_ADDRSTRLEN];
        inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr*)&client_addr), s, sizeof(s));
        printf("server: got connection from %s\n", s);
        #endif

        // welcome
        sendStatus(newsockfd, 220);
        while(1) {
            // get command from client
            srecv(newsockfd, buffer);
            
            // split input string on first space
            // then convert to all caps to get command
            char* str = strdup(buffer);
            char* cmd = strsep(&str, " ");
            stringToUpper(cmd);

            char* params;

            if (strncmp("USER", cmd, 4) == 0) {
                if (userLoggedIn) {
                    // already logged in, unrecognized command
                    sendStatus(newsockfd, 500);
                } else {
                    params = strsep(&str, " ");
                    params[strcspn(params, "\r\n")] = 0;

                    if (strcmp("cs317", params) == 0) {
                        sendStatus(newsockfd, 230);
                        userLoggedIn = 1;
                    } else {
                        sendStatus(newsockfd, 530);
                    }
                }
            } else if (strncmp("QUIT", cmd, 4) == 0) {
                sendStatus(newsockfd, 221);
                close(newsockfd);
                //TODO: replace with pthread kill
                break;
            } else if (strncmp("TYPE", cmd, 4) == 0) {
                if (!userLoggedIn) {
                    sendStatus(newsockfd, 530);
                    continue;
                }
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
                if (!userLoggedIn) {
                    sendStatus(newsockfd, 530);
                    continue;
                }

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
                if (!userLoggedIn) {
                    sendStatus(newsockfd, 530);
                    continue;
                }

                params = strsep(&str, " ");
                params[strcspn(params, "\r\n")] = 0; 
                stringToUpper(params);

                if (strcmp("F", params) == 0) {
                    // file structure, only mode necessary to implement
                    sendStatus(newsockfd, 200);
                } else {
                    sendStatus(newsockfd, 500);
                }
            } else if (strncmp("PASV", cmd, 4) == 0) {
                if (!userLoggedIn) {
                    sendStatus(newsockfd, 530);
                    continue;
                }

                // set up socket
                if ((datasockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
                    perror("error opening socket");
                    // exit(1);
                    // TODO: change to continue?
                }

                memset((char*) &data_addr, 0, sizeof(data_addr));
                data_addr.sin_family = AF_INET;
                data_addr.sin_addr.s_addr = INADDR_ANY;
                data_addr.sin_port = 0; // let bind() assign a port

                if (bind(datasockfd, (struct sockaddr*)&data_addr, sizeof(data_addr)) < 0) {
                    perror("error binding");
                    //exit(1);
                    // TODO: change to continue?
                }

                if (listen(datasockfd, BACKLOG) < 0) {
                    perror("error on listen");
                    //exit(1);
                    // TODO: change to continue?
                }

                struct sockaddr_in sin;
                socklen_t len = sizeof(sin);
                if (getsockname(datasockfd, (struct sockaddr *)&sin, &len) < 0) {
                    perror("error on getsockname");
                    // TODO: change to continue?
                } else {
                    data_port = ntohs(sin.sin_port);
                }

                // get IP address from control connection
                if (getsockname(newsockfd, (struct sockaddr*)&sin, &len) < 0) {
                    perror("error on getsockname");
                    // TODO: change to continue?
                } else {
                    data_ip = sin.sin_addr.s_addr;
                }

                #if DEBUG
                printf("data connection port: %d\n", data_port);
                #endif

                char outstr[FTP_MAX_LEN];
                sprintf(outstr, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\n",
                    (int) data_ip         & 0xff,
                    (int) (data_ip >>  8) & 0xff,
                    (int) (data_ip >> 16) & 0xff,
                    (int) (data_ip >> 24) & 0xff,
                    data_port / 256, 
                    data_port % 256);
                ssend(newsockfd, outstr);
            } else if (strncmp("RETR", cmd, 4) == 0) {
                if (!userLoggedIn) {
                    sendStatus(newsockfd, 530);
                    continue;
                }

                //client should connect before sending next command
                sin_size = sizeof(client_addr);
                newdatasockfd = accept(datasockfd, (struct sockaddr*)&data_client_addr, &data_sin_size);
                if (newdatasockfd == -1) {
                    perror("error on accept");
                    continue; // TODO: change?
                }

                #if DEBUG
                inet_ntop(data_client_addr.ss_family, get_in_addr((struct sockaddr*)&data_client_addr), data_s, sizeof(data_s));
                printf("server: data connected from %s\n", data_s);
                #endif

                char sendbuf[SEND_BUF_LEN]; 
                str[strcspn(str, "\r\n")] = 0; 
                
                FILE* fp;
                if ((fp= fopen(str, "r")) == NULL) {
                    perror("file not found.\n");
                    close(newdatasockfd);
                    sendStatus(newsockfd, 550);
                } else {
                    sendStatus(newsockfd, 125);
                    while(fread(sendbuf, sizeof(char), SEND_BUF_LEN, fp)) {
                        if (send(newdatasockfd, sendbuf, strlen(sendbuf), 0) < 0) {
                            perror("error on send");
                            // TODO: exit?
                        }
                        printf("sending, buf: %s\n", sendbuf);
                    }
                    printf("done sending\n");
                    sendStatus(newsockfd, 226);
                    close(newdatasockfd);
                }

            } else if (strncmp("NLST", cmd, 4) == 0) {
                if (!userLoggedIn) {
                    sendStatus(newsockfd, 530);
                    continue;
                }

                 // client should connect before sending next command
                //sin_size = sizeof(client_addr);
                newdatasockfd = accept(datasockfd, (struct sockaddr*)&data_client_addr, &data_sin_size);
                if (newdatasockfd == -1) {
                    perror("error on accept");
                    continue; // TODO: change?
                }

                #if DEBUG
                inet_ntop(data_client_addr.ss_family, get_in_addr((struct sockaddr*)&data_client_addr), data_s, sizeof(data_s));
                printf("server: data connected from %s\n", data_s);
                #endif

                // TODO: check if data connection exists
                sendStatus(newsockfd, 125);
                listFiles(newdatasockfd, ".");
                sendStatus(newsockfd, 226);
                close(newdatasockfd);
            } else {
                sendStatus(newsockfd, 500);
            }
        }
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


    int sockfd;
    struct sockaddr_in serv_addr;

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

    void* threads[NUM_THREADS];
    for (i = 0; i < NUM_THREADS; i++) {
        threads[i] = createThread(&mainLoop, (void*)&sockfd);
        runThread(threads[i], NULL);
    }

    // prevent exiting
    while(1) {};

    return 0;
}
