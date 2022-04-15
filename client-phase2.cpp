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
#include <algorithm>
#include <sys/unistd.h>
#include <sys/fcntl.h>

using namespace std ;

#define BACKLOG 50

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

class Node {
    public:

    string ID ;
    string uID ;
    string PORT ;
    int sockfd ;
    // request = 1 if initial request sent
    // request = 0 if initial request not sent yet
    int request ; 
    string reply ;
    
    Node() {
        request = 0 ;
        reply = "" ;
    }
} ;

bool cmp(Node* &nd1,Node *&nd2) {
    return stoi(nd1->ID)<stoi(nd2->ID);
}

int main(int argc, char *argv[])
{   
    string ID ;
    string uniqueID ;
    string PORT ;
    int neighbourCount ;
    int reqFileCount ;
    vector<Node*> neighbourList ;
    vector<pair<string,Node*> > reqFileList ;
    vector<string> ownFileList ;
    string directory ;

    Node* emptyNode = new Node() ;

    int receive_count = 0 ; // Number of nodes msg received from
    int connection_count = 0 ;

    ifstream configFile(argv[1]);
    directory = argv[2] ;

    configFile >> ID >> PORT >> uniqueID ;
    // configFile >> ID >> uniqueID ;
    configFile >> neighbourCount ;

    // cout << PORT << endl ;

    for (int i=0;i<neighbourCount;i++) {
        Node* node = new Node() ;
        configFile >> node->ID >> node->PORT ;
        neighbourList.push_back(node) ;
    }

    sort(neighbourList.begin(),neighbourList.end(),cmp) ;

    configFile >> reqFileCount ;

    for (int i=0;i<reqFileCount;i++) {
        string fileName ;

        configFile >> fileName ;

        reqFileList.push_back(make_pair(fileName,emptyNode)) ;
    }

    sort(reqFileList.begin(), reqFileList.end()) ;

    DIR *dir; struct dirent *diread;

    if ((dir = opendir(directory.data())) != nullptr) {
        while ((diread = readdir(dir)) != nullptr) {
            if ((string) diread->d_name != "." && (string) diread->d_name != "Downloaded" && (string) diread->d_name != "..") {
                ownFileList.push_back(diread->d_name);
            }
        }
        closedir (dir);
    } else {
        perror ("opendir");
        return EXIT_FAILURE;
    }

    sort(ownFileList.begin(),ownFileList.end());

    for (auto file : ownFileList) {
        cout << file << endl ;
    }

    if(neighbourCount==0) {
        for(auto file:reqFileList) {
            cout << "Found " + file.first + " at " ;
            cout << "0" ;
            cout << " with MD5 0 at depth " ;
            cout << "0" ;
            cout << endl ;
        }
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
    if (listen(listener, BACKLOG) == -1) {
        perror("listen");
        exit(3);
    }

    // add the listener to the master set
    FD_SET(listener, &master_read);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // now accept an incoming connection:

    for(int i=0;i<neighbourCount;i++) {
        struct addrinfo hints, *res;

        // first, load up address structs with getaddrinfo():

        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

        getaddrinfo(NULL, neighbourList[i]->PORT.c_str(), &hints, &res);

        // make a socket:

        neighbourList[i]->sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

        // connect!

        while(connect(neighbourList[i]->sockfd, res->ai_addr, res->ai_addrlen)<0) {
            close(neighbourList[i]->sockfd) ;
            neighbourList[i]->sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            sleep(2) ;
        }

        FD_SET(neighbourList[i]->sockfd, &master_write); // add to master set
        if (neighbourList[i]->sockfd > fdmax) {    // keep track of the max
            fdmax = neighbourList[i]->sockfd;
        }

        // cout << neighbour.second.c_str() << endl ;
    }

    // main loop
    for(;;) {
        read_fds = master_read; // copy it
        write_fds = master_write;
        if (select(fdmax+1, &read_fds, &write_fds, NULL, 0) == -1) {
            perror("select");
            exit(4);
        }
        // cout << "ABCDQQQQQ" << endl ;

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                        (struct sockaddr *)&remoteaddr,
                        &addrlen);

                    fcntl(newfd, F_SETFL, O_NONBLOCK);

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
                } 
                else {

                    size_t buf_idx = 0;
                    char buf[1024] ;

                    vector<string> list ;
                    string s ;

                    while (buf_idx < 1024)
                    {
                        int nbytes = recv(i, buf+buf_idx, 1, 0) ;
                        if(nbytes  == 0) {
                            printf("selectserver: socket %d hung up\n", i);
                            close(i); // bye!
                            FD_CLR(i, &master_read); // remove from master set
                        }
                        else if(nbytes < 0) {
                            break ;
                        }

                        if (buf_idx > 6 && '$' == buf[buf_idx] && '#' == buf[buf_idx - 1] && '&' == buf[buf_idx-2]  && buf[buf_idx-3] == '@' 
                        && '$' == buf[buf_idx-4] && '#' == buf[buf_idx - 5] && '&' == buf[buf_idx-6]  && buf[buf_idx-7] == '@'){
                            break;
                        }
                        else if (buf_idx > 2 && buf[buf_idx] == '$' && buf[buf_idx-1] == '#' && buf[buf_idx-2] == '&' && buf[buf_idx-3] == '@') {    
                            s = s.substr(0, s.length() - 3) ;
                            list.push_back(s) ;
                            s = "" ;
                        }
                        else {
                            s += buf[buf_idx] ;
                        }
                        
                        buf_idx++;
                    };

                    if(list.size()<4) {
                        continue  ;
                    }

                    // Identify nodes

                    Node* nd ;

                    for(int j=0;j<neighbourCount;j++) {
                        if(neighbourList[j]->ID == list[2]) {
                            neighbourList[j]->uID = list[1] ;
                            nd = neighbourList[j] ;
                            continue ;
                        }
                    }

                    if(list[0] == "Request") {
                        nd->reply = "Reply@&#$" + uniqueID + "@&#$" + ID + "@&#$" + PORT;
                        for (auto file: ownFileList) {
                            for (i=4;i<list.size();i++) {
                                if(list[i]==file) {
                                    nd->reply += "@&#$" + list[i] ;
                                    continue ;
                                }
                            }
                        }
                        nd->reply += "@&#$@&#$" ;

                        connection_count++ ;

                        if(connection_count==neighbourCount) {
                            for(auto node: neighbourList) {
                                cout << "Connected to " ;
                                cout << node->ID ;
                                cout << " with unique-ID " ;
                                cout << node->uID ;
                                cout << " on port " ;
                                cout << node->PORT ;
                                cout << endl ;
                            }   
                        }
                    }

                    else if (list[0] == "Reply") {
                        for (int j=0;j<reqFileCount;j++) {
                            for (int i=4;i<list.size();i++) {
                                if(list[i]==reqFileList[j].first) {
                                    if(reqFileList[j].second == emptyNode || stoi(list[1]) < stoi(reqFileList[j].second->uID)) {
                                        reqFileList[j].second = nd ;
                                    }
                                    continue ;
                                }
                            }
                        }

                        receive_count++ ;

                        if(receive_count == neighbourCount) {
                            for(auto file:reqFileList) {
                                cout << "Found " + file.first + " at " ;
                                if(file.second != emptyNode) {
                                    cout << file.second->uID ;
                                }
                                else cout << "0" ;
                                cout << " with MD5 0 at depth " ;
                                if(file.second != emptyNode) {
                                    cout << "1" ;
                                }
                                else cout << "0" ;
                                cout << endl ;
                            }
                        }
                    }
                } // END handle data from client
            } // END got new incoming connection

            else if (FD_ISSET(i, &write_fds)){
                for (int j=0;j<neighbourCount;j++) {
                    if(neighbourList[j]->sockfd==i) {
                        if(neighbourList[j]->request == 0) {
                            string msg = "Request@&#$" + uniqueID + "@&#$" + ID + "@&#$" + PORT;
                            for (auto file:reqFileList) {
                                msg += "@&#$" + file.first ;
                            }
                            msg += "@&#$@&#$" ;
                            if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                neighbourList[j]->request=1 ;
                            }
                        }
                        else if (neighbourList[j]->reply != "") {
                            string msg = neighbourList[j]->reply ;
                            if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                neighbourList[j]->reply="" ;
                            }
                        }
                        continue ;
                    }
                }
            }
        } // END looping through file descriptors
    } // END for(;;)--and you thought it would never end!


    return 0;
}