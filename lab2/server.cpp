/* Server code in C */

#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

using namespace std;

void readThread(int socketConn) {
  int n;
  char buffer[256];
  do {
    n = read(socketConn, buffer, 255);
    buffer[n] = '\0';
    cout << "\n" << buffer << endl;
  } while (strncmp(buffer, "chau", 4) != 0);

  close(socketConn);
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
    if (ConnectFD == -1) {
      perror("accept failed");
      continue;
    }
    cout << "Client connected" << endl;
    thread clientThread(readThread, ConnectFD);
    clientThread.detach();

    int n;
    do {
      cout << "Enter message for your client: ";
      getline(cin, buf); // Usar getline para permitir espacios

      n = write(ConnectFD, buf.c_str(), buf.size());
      if (n <= 0) {
        perror("write failed");
        break;
      }
    } while (buf.compare("chau") != 0);

    shutdown(ConnectFD, SHUT_RDWR);
    close(ConnectFD);
    cout << "Client disconnected" << endl;
  }

  close(SocketServer);
  return 0;
}
