/* Server code in C */

#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
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

map<string, int> m_clients;
mutex clientsMutex;

string padNumber(int num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

void readThread(int socketConn) {
  char type;
  ssize_t n;
  do {
    n = read(socketConn, &type, 1);

    if (n <= 0) {
      break;
    }

    switch (type) {
    case 'n': {
      string lenStr;
      for (int i = 0; i < 2; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int nickLen = stoi(lenStr);

      string nickname;
      for (int i = 0; i < nickLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        nickname += c;
      }

      lock_guard<mutex> lock(clientsMutex);

      if (m_clients.count(nickname)) {
        string err = "E015nicknameexiste";
        write(socketConn, err.c_str(), err.size());
        cout << "Cliente intentó usar nickname repetido: " << nickname << endl;
      } else {
        // Registrar nuevo cliente
        m_clients[nickname] = socketConn;
        cout << "Nuevo cliente registrado con nickname[" << type << lenStr
             << nickname << "]: " << nickname << endl;
      }

      break;
    }
    case 'm': {
      string msgLenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        msgLenStr += c;
      }
      int msgLen = stoi(msgLenStr);

      string msg;
      for (int i = 0; i < msgLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        msg += c;
      }

      string senderNick;
      {
        lock_guard<mutex> lock(clientsMutex);
        for (auto &p : m_clients) {
          if (p.second == socketConn) {
            senderNick = p.first;
            break;
          }
        }
      }

      cout << "Broadcast de [" << senderNick << "]" << msg << endl;

      lock_guard<mutex> lock(clientsMutex);
      string proto = "M" + padNumber(senderNick.size(), 2) + senderNick +
                     padNumber(msg.size(), 3) + msg;
      cout << "[" << proto << "]\n";
      for (auto &p : m_clients) {
        if (p.second != socketConn) {
          write(p.second, proto.c_str(), proto.size());
        }
      }
      break;
    }
    case 't': {
      string lenStr;
      for (int i = 0; i < 2; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int destLen = stoi(lenStr);

      string destNick;
      for (int i = 0; i < destLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        destNick += c;
      }

      string msgLenStr;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        msgLenStr += c;
      }
      int msgLen = stoi(msgLenStr);

      string msg;
      for (int i = 0; i < msgLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        msg += c;
      }

      // obtener nickname del emisor
      string senderNick;
      {
        lock_guard<mutex> lock(clientsMutex);
        for (auto &p : m_clients) {
          if (p.second == socketConn) {
            senderNick = p.first;
            break;
          }
        }
      }

      cout << "Mensaje privado de [" << senderNick << "] a [" << destNick
           << "]: " << msg << endl;

      lock_guard<mutex> lock(clientsMutex);
      auto it = m_clients.find(destNick);
      if (it != m_clients.end()) {
        int destSock = it->second;
        string proto = "T" + padNumber(senderNick.size(), 2) + senderNick +
                       padNumber(msg.size(), 3) + msg;
        cout << "[" << proto << "]\n";
        write(destSock, proto.c_str(), proto.size());
      } else {
        string err = "e03ERR";
        write(socketConn, err.c_str(), err.size());
      }
      break;
    }
    case 'l': {
      lock_guard<mutex> lock(clientsMutex);

      int numUsers = m_clients.size();
      string proto = "L" + padNumber(numUsers, 2);

      for (auto &p : m_clients) {
        const string &nick = p.first;
        proto += padNumber((int)nick.size(), 2) + nick;
      }

      cout << "Listado protocolo [" << proto << "]: " << "\n";
      write(socketConn, proto.c_str(), proto.size());
      break;
    }
    case 'f': {
      string lenStr;
      for (int i = 0; i < 2; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        lenStr += c;
      }
      int destLen = stoi(lenStr);

      string destNick;
      for (int i = 0; i < destLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        destNick += c;
      }

      string sizeFilename;
      for (int i = 0; i < 3; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        sizeFilename += c;
      }
      int msgLen = stoi(sizeFilename);

      string filename;
      for (int i = 0; i < msgLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        filename += c;
      }

      string sizeFileData;
      for (int i = 0; i < 10; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        sizeFileData += c;
      }
      long fileLen = stoi(sizeFileData);

      string fileData;
      for (int i = 0; i < fileLen; ++i) {
        char c;
        if (read(socketConn, &c, 1) <= 0)
          return;
        fileData += c;
      }

      string senderNick;
      {
        lock_guard<mutex> lock(clientsMutex);
        for (auto &p : m_clients) {
          if (p.second == socketConn) {
            senderNick = p.first;
            break;
          }
        }
      }

      lock_guard<mutex> lock(clientsMutex);
      auto it = m_clients.find(destNick);
      if (it != m_clients.end()) {
        int destSock = it->second;
        string proto = "F" + padNumber(destNick.size(), 2) + destNick +
                       padNumber(filename.size(), 3) + filename +
                       padNumber(fileData.size(), 10) + fileData;
        cout << "[" << proto << "]\n";
        write(destSock, proto.c_str(), proto.size());
      } else {
        string err = "e03ERR";
        write(socketConn, err.c_str(), err.size());
      }
      break;
    }
    case 'x': {
      string nick;
      for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it->second == socketConn) {
          nick = it->first;
          m_clients.erase(it);
          break;
        }
      }
      close(socketConn);
      cout << "Cliente " << nick << " desconectado." << endl;
      break;
    }
    default:
      cout << "Tipo de mensaje desconocido: " << type << endl;
      break;
    }

  } while (true);

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

  // 🔹 Permitir reutilizar el puerto inmediatamente tras cerrar el servidor
  int opt = 1;
  if (setsockopt(SocketServer, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
      0) {
    perror("setsockopt(SO_REUSEADDR) failed");
    close(SocketServer);
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
  }

  close(SocketServer);
  return 0;
}