#include <string.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
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
#include <set>
#include <fstream>
#include <filesystem>

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

class Node {
    public:

    string ID ;
    string uID ;
    string PORT ;
    int sockfd ;

    // request = 1 if initial request sent
    // request = 0 if initial request not sent yet
    int request ; 
    int rep ;
    string reply ;
    fstream indata;

    // If not sending file 0, if sending file 1

    int sending = 0;
    //string reply2 ;
    

    Node() {
        request = 0 ;
        reply = "" ;
        //reply2 = "" ;
        rep = 1 ;
    }

    void AppendReply(string s) {
        reply.append(s) ;
    }
} ;

int main(int argc, char *argv[])
{   
    string ID ;
    string uniqueID ;
    string PORT ;
    int neighbourCount ;
    int reqFileCount ;
    vector<Node*> neighbourList ;
    vector<pair<pair<string,Node*>,int> > reqFileList ;
    vector<string> ownFileList ;
    string directory ;

    Node* emptyNode = new Node() ;

    int receive_count = 0 ; // Number of nodes msg received from

    int relay_count = 0 ; // Number of nodes msg relayed

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

    configFile >> reqFileCount ;

    for (int i=0;i<reqFileCount;i++) {
        string fileName ;

        configFile >> fileName ;

        reqFileList.push_back(make_pair(make_pair(fileName,emptyNode),0)) ;
    }

    sort(reqFileList.begin(), reqFileList.end()) ;

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

    // for (auto file : ownFileList) {
    //     cout << file << endl ;
    // }

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

    char buf[4096];    // buffer for client data
    int nbytes;

    for(int i=0;i<4096;i++){

        buf[i]='\0';
    }

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

    vector<Node*> newly_formed;

    for(;;) {
        read_fds = master_read; // copy it
        write_fds = master_write;
        if (select(fdmax+1, &read_fds, &write_fds, NULL, &tv) == -1) {
            perror("select");
            exit(4);
        }

        //cout<<"HEHE"<<endl;
        // cout << "ABCDQQQQQ" << endl ;

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                
                /*if(find(newly_formed.begin(),newly_formed.end(),i)!=newly_formed.end()){

                    continue;
                }*/

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
                    // handle data from a client
                    if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
                        // got error or connection closed by client
                        //cout<<nbytes<<endl;
                        
                        if (nbytes == 0) {
                            // connection closed
                            cout<<"selectserver: socket " << i<< " hung up"<<endl;
                        } else {
                            continue ;
                        }
                        close(i); // bye!
                        FD_CLR(i, &master_read); // remove from master set
                    } else {

                        // Break msg
                        //cout<<nbytes<<endl;

                        string msg = (string) buf ;

                        //cout<<"KYAAA"<<endl;

                        vector<string> list ;

                        auto start = 0;

                        while(msg[start]=='R'){
    
                            list.clear();
                            //cout<<"HULAHULA"<<endl;

                            auto origstart = start;
                            
                            auto end = msg.find("#",start);
                            while (end != string::npos)
                            {
                                if(msg[start]=='#') break ;
                                list.push_back(msg.substr(start, end - start)) ;
                                start = end + 1;
                                end = msg.find("#", start);
                            }
                            auto origend = start;
                            start++;

                            //cout<<start<<endl;

                            /*cout<<"Received: ";

                            for(int i=0;i<list.size();i++){

                                cout<<list[i]<<" ";
                            }
                            cout<<"  "<<msg;

                            cout<<endl;*/

                            // Identify nodes

                            Node* nd1 ;

                            /*if(list.size()>=3){

                                cout<<"List[2]: "<<list[2]<<endl;
                            }*/


                            for(int j=0;j<neighbourCount;j++) {
                                if(neighbourList[j]->ID == list[2]) {
                                    neighbourList[j]->uID = list[1] ;
                                    nd1 = neighbourList[j] ;
                                    continue ;
                                }
                            }

                            //cout<<"ND1 is: "<<nd1->ID<<" "<<nd1->uID<<endl;

                            if(list[0] == "Request1") {
                                nd1->reply = "Reply1#" + uniqueID + "#" + ID + "#" + PORT;
                                string request2 = "Request2#" + list[1] + "#" + list[2] + "#" + list[3] + "#" + uniqueID + "#" + ID + "#" + PORT;
                                
                                set<string> sto;
                                for (auto file: ownFileList) {
                                    for (i=4;i<list.size();i++) {
                                        if(list[i]==file) {
                                            nd1->reply += "#" + list[i] ;
                                            continue ;
                                        }else{
                                            sto.insert(list[i]);
                                            //request2 += "#" + list[i] ;
                                            continue ;
                                        }
                                    }
                                }

                                for(auto x:sto){

                                    request2 += "#" + x;
                                }
                                if(neighbourCount==1) {

                                    nd1->reply+="#FINISH";
                                }
                                nd1->reply += "##" ;
                                request2 += "##";

                                for(int j=0;j<neighbourCount;j++){

                                    if(neighbourList[j]==nd1){

                                        continue;
                                    }

                                    neighbourList[j]->reply = request2;
                                }
                                for(int i = 0; i <= fdmax; i++){

                                    if (FD_ISSET(i, &write_fds)){
                                        for (int j=0;j<neighbourCount;j++) {
                                            if(neighbourList[j]->sockfd==i) {
                                                if(neighbourList[j]->request == 0) {
                                                    string msg = "Request1#" + uniqueID + "#" + ID + "#" + PORT;
                                                    for (auto file:reqFileList) {
                                                        msg += "#" + file.first.first ;
                                                    }
                                                    msg += "##" ;
                                                    if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                                        neighbourList[j]->request=1 ;
                                                        //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                                                    }
                                                }
                                                else if (neighbourList[j]->reply != "") {
                                                    string msg = neighbourList[j]->reply ;
                                                    if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                                        //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                                                        neighbourList[j]->reply="" ;
                                                    }
                                                }
                                                continue ;
                                            }
                                        }
                                    }
                                } // END looping through file descriptors
                            }

                            else if (list[0] == "Reply1") {
                                for (int j=0;j<reqFileCount;j++) {
                                    for (int i=4;i<list.size();i++) {
                                        if(list[i]==reqFileList[j].first.first) {
                                            if(reqFileList[j].first.second == emptyNode || list[1] < reqFileList[j].first.second->uID) {
                                                reqFileList[j].first.second = nd1 ;
                                                //cout<<list[i]<<" "<<nd1->ID<<endl;

                                                reqFileList[j].second = 1;
                                            }
                                            continue ;
                                        }
                                    }
                                }

                                if(list.back()=="FINISH"){

                                    receive_count++ ;
                                }

                                if(receive_count == neighbourCount) {
                                    for(auto file:reqFileList) {
                                        cout << "Found " + file.first.first + " at " ;
                                        if(file.first.second != emptyNode) {
                                            cout << file.first.second->uID ;
                                        }
                                        else cout << "0" ;
                                        cout << " with MD5 0 at depth " ;
                                        cout << file.second ;
                                        cout << endl ;
                                    }
                                }

                                //receive_count++ ;

                                /*if(receive_count == neighbourCount) {
                                    for(auto file:reqFileList) {
                                        cout << "Found " + file.first.first + " at " ;
                                        if(file.first.second != emptyNode) {
                                            cout << file.first.second->uID ;
                                        }
                                        else cout << "0" ;
                                        cout << " with MD5 0 at depth " ;
                                        if(file.first.second != emptyNode) {
                                            cout << "1" ;
                                        }
                                        else cout << "0" ;
                                        cout << endl ;
                                    }
                                }*/
                            }

                            else if(list[0]=="Request2") {

                                Node *nd2;

                                for(int j=0;j<neighbourCount;j++) {
                                    if(neighbourList[j]->ID == list[5]) {
                                        neighbourList[j]->uID = list[4] ;
                                        nd2 = neighbourList[j] ;
                                        continue ;
                                    }
                                }

                                nd2->reply = "Relay#" + uniqueID + "#" + ID + "#" + PORT + "#" + list[1] + "#" +list[2] +"#" +list[3];

                                for (auto file: ownFileList) {
                                    for (i=7;i<list.size();i++) {
                                        if(list[i]==file) {
                                            nd2->reply += "#" + list[i] ;
                                            continue ;
                                        }
                                    }
                                }
                                
                                nd2->reply += "##";
                                for(int i = 0; i <= fdmax; i++){

                                    if (FD_ISSET(i, &write_fds)){
                                        for (int j=0;j<neighbourCount;j++) {
                                            if(neighbourList[j]->sockfd==i) {
                                                if(neighbourList[j]->request == 0) {
                                                    string msg = "Request1#" + uniqueID + "#" + ID + "#" + PORT;
                                                    for (auto file:reqFileList) {
                                                        msg += "#" + file.first.first ;
                                                    }
                                                    msg += "##" ;
                                                    if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                                        neighbourList[j]->request=1 ;
                                                        //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                                                    }
                                                }
                                                else if (neighbourList[j]->reply != "") {
                                                    string msg = neighbourList[j]->reply ;
                                                    if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                                        //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                                                        neighbourList[j]->reply="" ;
                                                    }
                                                }
                                                continue ;
                                            }
                                        }
                                    }
                                } // END looping through file descriptors
                                
                            }

                            else if(list[0]=="Reply2") {

                                Node *nd2;

                                for(int j=0;j<neighbourCount;j++) {
                                    if(neighbourList[j]->ID == list[5]) {
                                        neighbourList[j]->uID = list[4] ;
                                        nd2 = neighbourList[j] ;
                                        continue ;
                                    }
                                }

                                
                                for (int j=0;j<reqFileCount;j++) {
                                    for (int i=7;i<list.size();i++) {
                                        if(list[i]==reqFileList[j].first.first) {
                                            if(reqFileList[j].first.second == emptyNode || (reqFileList[j].second==2 && list[1] < reqFileList[j].first.second->uID)) {
                                                
                                                Node *nd3 = new Node();
                                                nd3-> uID= list[1] ;
                                                nd3 -> ID = list[2] ;
                                                nd3 -> PORT= list[3] ;
                                                

                                                reqFileList[j].first.second =nd3;
                                                
                                                //cout<<list[i]<<" "<<nd1->ID<<endl;
                                                reqFileList[j].second=2;
                                            }
                                            continue ;
                                        }
                                    }
                                }    

                                if(list.back()=="FINISH"){

                                    receive_count++;
                                }

                                if(receive_count == neighbourCount) {
                                    for(auto file:reqFileList) {
                                        cout << "Found " + file.first.first + " at " ;
                                        if(file.first.second != emptyNode) {
                                            cout << file.first.second->uID ;
                                        }
                                        else cout << "0" ;
                                        cout << " with MD5 0 at depth " ;
                                        cout << file.second ;
                                        cout << endl ;
                                    }
                                }
                            } 
                            
                            else if(list[0]=="Relay") {

                                relay_count++;
                                
                                Node *nd2;

                                for(int j=0;j<neighbourCount;j++) {
                                    if(neighbourList[j]->ID == list[5]) {
                                        neighbourList[j]->uID = list[4] ;
                                        nd2 = neighbourList[j] ;
                                        continue ;
                                    }
                                }

                                nd2->reply = "Reply2#" + list[1] + "#" +list[2] +"#" +list[3] + "#"+ uniqueID + "#" + ID + "#" + PORT ;
                                
                                for (i=7;i<list.size();i++) {
                                    nd2->reply += "#" + list[i] ;
                                }
                                if(neighbourCount*(neighbourCount-1)==relay_count){
                                    nd2->reply+="#FINISH##";
                                    for(int j=0;j<neighbourCount;j++){

                                        if(neighbourList[j]==nd2){

                                            continue;
                                        }

                                        neighbourList[j]->reply = "Reply2#FINISH##";
                                    }
                                }else{

                                    nd2->reply+="##";
                                }
                                for(int i = 0; i <= fdmax; i++){

                                    if (FD_ISSET(i, &write_fds)){
                                        for (int j=0;j<neighbourCount;j++) {
                                            if(neighbourList[j]->sockfd==i) {
                                                if(neighbourList[j]->request == 0) {
                                                    string msg = "Request1#" + uniqueID + "#" + ID + "#" + PORT;
                                                    for (auto file:reqFileList) {
                                                        msg += "#" + file.first.first ;
                                                    }
                                                    msg += "##" ;
                                                    if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                                        neighbourList[j]->request=1 ;
                                                        //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                                                    }
                                                }
                                                else if (neighbourList[j]->reply != "") {
                                                    string msg = neighbourList[j]->reply ;
                                                    if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                                        //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                                                        neighbourList[j]->reply="" ;
                                                    }
                                                }
                                                continue ;
                                            }
                                        }
                                    }
                                } // END looping through file descriptors
                            }

                            else if(list[0] == "RSend") {

                                Node * sendto=emptyNode;
                                
                                for(int j=0;j<neighbourCount;j++){

                                    if(neighbourList[j]->ID==list[2]) {

                                        sendto=neighbourList[j];
                                    }
                                }

                                ifstream indata;

                                indata.open(directory+list[4]);

                                char buf2[256];

                                while (!indata.eof() ) { // keep reading until end-of-file
                                    
                                    indata.read(buf2,200); // sets EOF flag if no value found

                                    string msg = "Receive#"+uniqueID+"#"+ID+"#"+PORT+"#"+list[4]+"#";

                                    for(int i=0;i<200;i++){

                                        msg.push_back(buf2[i]);
                                    }

                                    msg+="##";

                                    sendto->reply = msg;

                                    for(i = 0; i <= fdmax; i++){

                                        if (FD_ISSET(i, &write_fds)){
                                            
                                            /*if(find(newly_formed.begin(),newly_formed.end(),i)!=newly_formed.end()){

                                                continue;
                                            }*/

                                            for (int j=0;j<newly_formed.size();j++) {
                                                if(newly_formed[j]->sockfd==i) {
                                                    if (newly_formed[j]->reply != "") {
                                                        string msg = newly_formed[j]->reply ;
                                                        if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                                            //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                                                            newly_formed[j]->reply="" ;
                                                        }
                                                    }
                                                    continue ;
                                                }
                                            }
                                        }
                                    } // END looping through file descriptors
                                }

                                newly_formed.clear();
                            } 

                            else if(list[0] == "Receive") {

                                string downloads = directory+"Downloads/";

                                try{

                                    mkdir(downloads.c_str(),0777);

                                }catch(exception e){

                                }

                                fstream filewrite;
                                filewrite.open(downloads+list[4],ios_base::app);

                                string data="";

                                int val=0;

                                for(int j=origstart;j<=origend-2;j++){

                                    if(val!=5&&msg[j]=='#'){

                                        val++;
                                        continue;
                                    }

                                    data.push_back(msg[j]);
                                }

                                filewrite<<data;
                            }
                        }
                        for(int i=0;i<4096;i++){

                            buf[i]='\0';
                        }
                    }
                } // END handle data from client
            } // END got new incoming connection
        }

        //cout<<relay_count<<" "<<receive_count<<endl;

        for(i = 0; i <= fdmax; i++){

            if (FD_ISSET(i, &write_fds)){
                
                /*if(find(newly_formed.begin(),newly_formed.end(),i)!=newly_formed.end()){

                    continue;
                }*/

                for (int j=0;j<neighbourCount;j++) {
                    if(neighbourList[j]->sockfd==i) {
                        if(neighbourList[j]->request == 0) {
                            string msg = "Request1#" + uniqueID + "#" + ID + "#" + PORT;
                            for (auto file:reqFileList) {
                                msg += "#" + file.first.first ;
                            }
                            msg += "##" ;
                            if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                neighbourList[j]->request=1 ;
                                //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                            }
                        }
                        else if (neighbourList[j]->reply != "") {
                            string msg = neighbourList[j]->reply ;
                            if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                                neighbourList[j]->reply="" ;
                            }
                        }
                        continue ;
                    }
                }

                for (int j=0;j<newly_formed.size();j++) {
                    if(newly_formed[j]->sockfd==i) {
                        if(newly_formed[j]->request == 0) {
                            string msg = "Request1#" + uniqueID + "#" + ID + "#" + PORT;
                            for (auto file:reqFileList) {
                                msg += "#" + file.first.first ;
                            }
                            msg += "##" ;
                            if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                neighbourList[j]->request=1 ;
                                //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                            }
                        }
                        else if (neighbourList[j]->reply != "") {
                            string msg = neighbourList[j]->reply ;
                            if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                                neighbourList[j]->reply="" ;
                            }
                        }
                        continue ;
                    }
                }
            }
        } // END looping through file descriptors

        if(receive_count != neighbourCount -1){
            continue;
        }

        pair<pair<string,Node*>,int> file;

        file.second=0;

        for(auto x:reqFileList) {
            if(x.second!=0){
                
                if(x.second==2&&!newly_formed.empty()){

                    continue;
                }
                file=x;
                x.second=0;
                break;
            }
        }

        Node* trans=emptyNode;

        if(file.second==1){

            for(int j=0;j<neighbourCount;j++){

                if(neighbourList[j]->uID==file.first.second->uID){

                    trans=neighbourList[j];
                } 
            }
            trans->reply = "RSend#"+uniqueID+"#"+ID+"#"+PORT+"#"+file.first.first+"##";

        }else if(file.second==2){

            struct addrinfo hints, *res;

            // first, load up address structs with getaddrinfo():

            Node *newnode = new Node();

            newnode = file.first.second;

            memset(&hints, 0, sizeof hints);
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

            getaddrinfo(NULL, newnode->PORT.c_str(), &hints, &res);

            // make a socket:

            newnode->sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

            // connect!

            while(connect(newnode->sockfd, res->ai_addr, res->ai_addrlen)<0) {
                close(newnode->sockfd) ;
                newnode->sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
                sleep(2) ;
            }

            FD_SET(newnode->sockfd, &master_write); // add to master set
            if (newnode->sockfd > fdmax) {    // keep track of the max
                fdmax = neighbourList[i]->sockfd;
            }

            newly_formed.push_back(newnode);

            newnode->reply = "RSend#"+uniqueID+"#"+ID+"#"+PORT+"#"+file.first.first+"##";

            
            //; Create a new socket, pass socket descriptor in the message, and on receive side send data to the same socket
        }else{

            continue;
        }

        for(i = 0; i <= fdmax; i++){

            if (FD_ISSET(i, &write_fds)){
                
                /*if(find(newly_formed.begin(),newly_formed.end(),i)!=newly_formed.end()){

                    continue;
                }*/

                for (int j=0;j<neighbourCount;j++) {
                    if(neighbourList[j]->sockfd==i) {
                        if(neighbourList[j]->request == 0) {
                            string msg = "Request1#" + uniqueID + "#" + ID + "#" + PORT;
                            for (auto file:reqFileList) {
                                msg += "#" + file.first.first ;
                            }
                            msg += "##" ;
                            if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                neighbourList[j]->request=1 ;
                                //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
                            }
                        }
                        else if (neighbourList[j]->reply != "") {
                            string msg = neighbourList[j]->reply ;
                            if (send(i, msg.c_str(), msg.length(), 0) > 0) {
                                //cout<<msg<<" sent to "<< neighbourList[j]->ID <<endl;
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