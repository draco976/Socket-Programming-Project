#include <string.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

using namespace std ;

#define BACKLOG 10

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{   
    string ID ;
    string uniqueID ;
    int neighbourCount ;
    int reqFileCount ;
    vector< pair<int, string> > neighbourList ;
    vector<string> reqFileList ;
    vector<string> ownFileList ;
    string directory ;

    ifstream configFile(argv[1]);
    directory = argv[2] ;

    string PORT ;

    configFile >> ID >> PORT >> uniqueID ;
    // configFile >> ID >> uniqueID ;
    configFile >> neighbourCount ;

    cout << PORT << endl ;

    for (int i=0;i<neighbourCount;i++) {
        int nID ;
        string nPort ;

        configFile >> nID >> nPort ;

        neighbourList.push_back(make_pair(nID,nPort)) ;
    }

    configFile >> reqFileCount ;

    for (int i=0;i<reqFileCount;i++) {
        string fileName ;

        configFile >> fileName ;

        reqFileList.push_back(fileName) ;
    }

    DIR *dir; struct dirent *diread;

    if ((dir = opendir(directory.data())) != nullptr) {
        while ((diread = readdir(dir)) != nullptr) {
            if ((string) diread->d_name != "." && (string) diread->d_name != "..") {
                ownFileList.push_back(diread->d_name);
            }
        }
        closedir (dir);
    } else {
        perror ("opendir");
        return EXIT_FAILURE;
    }

    for (auto file : ownFileList) {
        cout << file << endl ;
    }

    fd_set master_read;    // master file descriptor list
    fd_set master_write;
    fd_set read_fds;  // temp file descriptor list for select()
    fd_set write_fds;
    int fdmax;        // maximum file descriptor number

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    char buf[256];    // buffer for client data
    int nbytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    int i, j, rv;

    struct addrinfo hints, *ai, *p;

    FD_ZERO(&master_read);    // clear the master and temp sets
    FD_ZERO(&master_write); 
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    // !! don't forget your error checking for these calls !!

    // first, load up address structs with getaddrinfo():

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

    if ((rv = getaddrinfo(NULL, PORT.c_str(), &hints, &ai)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    // make a socket, bind it, and listen on it:

    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) { 
            continue;
        }
        
        // lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;
    }

    freeaddrinfo(ai); // all done with this

    // listen
    if (listen(listener, 10) == -1) {
        perror("listen");
        exit(3);
    }

    // add the listener to the master set
    FD_SET(listener, &master_read);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // now accept an incoming connection:
    

    int sockfdList[neighbourList.size()] ;

    for(auto neighbour:neighbourList) {
        struct addrinfo hints, *res;
        int sockfd;

        // first, load up address structs with getaddrinfo():

        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

        getaddrinfo(NULL, neighbour.second.c_str(), &hints, &res);

        // make a socket:

        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

        // connect!

        while(connect(sockfd, res->ai_addr, res->ai_addrlen)<0) {
            close(sockfd) ;
            sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            sleep(2) ;
        }

        FD_SET(sockfd, &master_write); // add to master set
        if (sockfd > fdmax) {    // keep track of the max
            fdmax = sockfd;
        }

        // cout << neighbour.second.c_str() << endl ;
    }

    int done[10000] ;
    for(i=0;i<10000;i++) {
        done[i] = 0 ;
    }

    // main loop
    for(;;) {
        read_fds = master_read; // copy it
        write_fds = master_write;
        if (select(fdmax+1, &read_fds, &write_fds, NULL, &tv) == -1) {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                        (struct sockaddr *)&remoteaddr,
                        &addrlen);

                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(newfd, &master_read); // add to master set
                        if (newfd > fdmax) {    // keep track of the max
                            fdmax = newfd;
                        }
                        // printf("selectserver: new connection from %s on "
                        //     "socket %d\n",
                        //     inet_ntop(remoteaddr.ss_family,
                        //         get_in_addr((struct sockaddr*)&remoteaddr),
                        //         remoteIP, INET6_ADDRSTRLEN),
                        //     newfd);
                    }
                } else {
                    // handle data from a client
                    if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
                        // got error or connection closed by client
                        if (nbytes == 0) {
                            // connection closed
                            printf("selectserver: socket %d hung up\n", i);
                        } else {
                            perror("recv");
                        }
                        close(i); // bye!
                        FD_CLR(i, &master_read); // remove from master set
                    } else {

                        cout << "connected to " ;
                        cout << buf[5] ;
                        cout << " with unique-ID " ;
                        for(j=0;j<4;j++) {
                            cout << buf[j] ;
                        }
                        cout << " on port " ;
                        for(j=7;j<11;j++) {
                            cout << buf[j] ;
                        }
                        cout << endl ;
                    }
                } // END handle data from client
            } // END got new incoming connection

            else if (FD_ISSET(i, &write_fds)){
                if(done[i]==0) {
                    done[i]=1 ;
                    string msg = uniqueID + "#" + ID + "#" + PORT;
                    if (send(i, msg.c_str(), msg.length(), 0)<=0) {
                        done[i]=0 ;
                    }
                }
            }
        } // END looping through file descriptors
    } // END for(;;)--and you thought it would never end!

    

    return 0;
}