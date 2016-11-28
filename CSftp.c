#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "dir.h"
#include "usage.h"

#define BACKLOG 10


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
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
    }


    // This is how to call the function in dir.c to get a listing of a directory.
    // It requires a file descriptor, so in your code you would pass in the file descriptor 
    // returned for the ftp server's data connection
    
    printf("Printed %d directory entries\n", listFiles(1, "."));
    return 0;

}
