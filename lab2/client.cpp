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
  int n;
  char buffer[256];
  do {
    n = read(socketConn, buffer, 255);
    buffer[n] = '\n';
    cout<<"\n" << buffer <<endl;
  } while(strncmp(buffer, "chau",4) != 0);
}

int main(void) {
  struct sockaddr_in stSockAddr;
  int Res;
  int SocketCli = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  int n;
  char buffer[256];
  string buf;

  if (-1 == SocketCli) {
    perror("cannot create socket");
    exit(EXIT_FAILURE);
  }

  memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

  stSockAddr.sin_family = AF_INET;
  stSockAddr.sin_port = htons(45000);
  Res = inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

  if (-1 == connect(SocketCli, (const struct sockaddr *)&stSockAddr,
                    sizeof(struct sockaddr_in))) {
    perror("connect failed");
    close(SocketCli);
    exit(EXIT_FAILURE);
  }

  std::thread(readThread, SocketCli).detach();

  do {
    cout << "Enter message for the server: ";
    cin >> buf;
    n = write(SocketCli, buf.c_str(), buf.size());
 
  } while (buf.compare("chau") != 0);
  
  shutdown(SocketCli, SHUT_RDWR);


  close(SocketCli);
  return 0;
}