/* Server code in C */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include <iostream>
#include <thread>

using namespace std;

void readThread(int socketConn){
  int n;1
  char buffer[256];
  do {
    n = read(socketConn, buffer, 255);
    buffer[n] = '\0';
    cout<<"\n" << buffer <<endl;
  } while(strncmp(buffer, "chau",4) != 0);
}


int main(void) {
  struct sockaddr_in stSockAddr;
  int SocketServer = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  char buffer[256];
  int n;
  string buf;

  if (-1 == SocketServer) {
    perror("can not create socket");
    exit(EXIT_FAILURE);
  }

  memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

  stSockAddr.sin_family = AF_INET;
  stSockAddr.sin_port = htons(45000);
  stSockAddr.sin_addr.s_addr = INADDR_ANY;

  if (-1 == bind(SocketServer, (const struct sockaddr *)&stSockAddr,
                 sizeof(struct sockaddr_in))) {
    perror("error bind failed");
    close(SocketServer);
    exit(EXIT_FAILURE);
  }

  if (-1 == listen(SocketServer, 10)) {
    perror("error listen failed");
    close(SocketServer);
    exit(EXIT_FAILURE);
  }

  for (;;) {

    int ConnectFD = accept(SocketServer, NULL, NULL);
    thread(readThread, SocketServer).detach();
    do {
      cout << "Enter message for you client: ";
      cin >> buf;
      n = write(ConnectFD, buf.c_str(), buf.size());
    } while (buf.compare("chau") != 0);
    // printf("Here is the message: [%s]\n", buffer);
    shutdown(ConnectFD, SHUT_RDWR);
    close(ConnectFD);



  }

  close(SocketServer);
  return 0;
}
