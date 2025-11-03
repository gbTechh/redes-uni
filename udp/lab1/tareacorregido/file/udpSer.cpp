/* Server code in C++ - UDP Version */

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

#define PORT 45000
#define BUFFER_SIZE 65507 // Máximo tamaño para UDP

struct ClientInfo {
  struct sockaddr_in address;
  string nickname;
};

map<string, ClientInfo> m_clients;
mutex clientsMutex;

string padNumber(int num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

void sendToClient(int sockfd, const string &message,
                  const struct sockaddr_in &clientAddr) {
  sendto(sockfd, message.c_str(), message.size(), 0,
         (const struct sockaddr *)&clientAddr, sizeof(clientAddr));
}

void handleClientMessage(int sockfd, const string &message,
                         const struct sockaddr_in &clientAddr) {
  if (message.empty())
    return;

  char type = message[0];

  switch (type) {
  case 'n': {
    if (message.size() < 3)
      return;

    string lenStr = message.substr(1, 2);
    int nickLen = stoi(lenStr);

    if (message.size() < 3 + nickLen)
      return;
    string nickname = message.substr(3, nickLen);

    lock_guard<mutex> lock(clientsMutex);

    if (m_clients.count(nickname)) {
      string err = "E015nicknameexiste";
      sendToClient(sockfd, err, clientAddr);
      cout << "Cliente intentó usar nickname repetido: " << nickname << endl;
    } else {
      ClientInfo clientInfo;
      clientInfo.address = clientAddr;
      clientInfo.nickname = nickname;
      m_clients[nickname] = clientInfo;
      cout << "Nuevo cliente registrado: " << nickname << endl;
    }
    break;
  }
  case 'm': {
    if (message.size() < 4)
      return;

    string msgLenStr = message.substr(1, 3);
    int msgLen = stoi(msgLenStr);

    if (message.size() < 4 + msgLen)
      return;
    string msg = message.substr(4, msgLen);

    string senderNick;
    {
      lock_guard<mutex> lock(clientsMutex);
      for (auto &p : m_clients) {
        if (p.second.address.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
            p.second.address.sin_port == clientAddr.sin_port) {
          senderNick = p.first;
          break;
        }
      }
    }

    if (senderNick.empty())
      return;

    cout << "Broadcast de [" << senderNick << "]" << msg << endl;

    lock_guard<mutex> lock(clientsMutex);
    string proto = "M" + padNumber(senderNick.size(), 2) + senderNick +
                   padNumber(msg.size(), 3) + msg;
    for (auto &p : m_clients) {
      if (p.first != senderNick) {
        sendToClient(sockfd, proto, p.second.address);
      }
    }
    break;
  }
  case 't': {
    if (message.size() < 3)
      return;

    string lenStr = message.substr(1, 2);
    int destLen = stoi(lenStr);

    if (message.size() < 3 + destLen)
      return;
    string destNick = message.substr(3, destLen);

    if (message.size() < 3 + destLen + 3)
      return;
    string msgLenStr = message.substr(3 + destLen, 3);
    int msgLen = stoi(msgLenStr);

    if (message.size() < 3 + destLen + 3 + msgLen)
      return;
    string msg = message.substr(3 + destLen + 3, msgLen);

    string senderNick;
    {
      lock_guard<mutex> lock(clientsMutex);
      for (auto &p : m_clients) {
        if (p.second.address.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
            p.second.address.sin_port == clientAddr.sin_port) {
          senderNick = p.first;
          break;
        }
      }
    }

    if (senderNick.empty())
      return;

    cout << "Mensaje privado de [" << senderNick << "] a [" << destNick
         << "]: " << msg << endl;

    lock_guard<mutex> lock(clientsMutex);
    auto it = m_clients.find(destNick);
    if (it != m_clients.end()) {
      string proto = "T" + padNumber(senderNick.size(), 2) + senderNick +
                     padNumber(msg.size(), 3) + msg;
      sendToClient(sockfd, proto, it->second.address);
    } else {
      string err = "e03ERR";
      sendToClient(sockfd, err, clientAddr);
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

    cout << "Listado usuarios solicitado" << endl;
    sendToClient(sockfd, proto, clientAddr);
    break;
  }
  case 'f': {
    if (message.size() < 3)
      return;

    string lenStr = message.substr(1, 2);
    int destLen = stoi(lenStr);

    if (message.size() < 3 + destLen)
      return;
    string destNick = message.substr(3, destLen);

    if (message.size() < 3 + destLen + 3)
      return;
    string sizeFilename = message.substr(3 + destLen, 3);
    int filenameLen = stoi(sizeFilename);

    if (message.size() < 3 + destLen + 3 + filenameLen)
      return;
    string filename = message.substr(3 + destLen + 3, filenameLen);

    if (message.size() < 3 + destLen + 3 + filenameLen + 10)
      return;
    string sizeFileData = message.substr(3 + destLen + 3 + filenameLen, 10);
    long fileLen = stol(sizeFileData);

    if (message.size() < 3 + destLen + 3 + filenameLen + 10 + fileLen)
      return;
    string fileData =
        message.substr(3 + destLen + 3 + filenameLen + 10, fileLen);

    string senderNick;
    {
      lock_guard<mutex> lock(clientsMutex);
      for (auto &p : m_clients) {
        if (p.second.address.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
            p.second.address.sin_port == clientAddr.sin_port) {
          senderNick = p.first;
          break;
        }
      }
    }

    if (senderNick.empty())
      return;

    cout << "Archivo [" << filename << "] de [" << senderNick << "] para ["
         << destNick << "], tamaño: " << fileLen << " bytes" << endl;

    lock_guard<mutex> lock(clientsMutex);
    auto it = m_clients.find(destNick);
    if (it != m_clients.end()) {
      string proto = "F" + padNumber(senderNick.size(), 2) + senderNick +
                     padNumber(filename.size(), 3) + filename +
                     padNumber(fileData.size(), 10) + fileData;

      // Verificar si el archivo es demasiado grande para UDP
      if (proto.size() > BUFFER_SIZE) {
        cout << "Advertencia: Archivo muy grande para UDP (" << proto.size()
             << " bytes)" << endl;
        string err = "E99Archivo demasiado grande";
        sendToClient(sockfd, err, clientAddr);
      } else {
        sendToClient(sockfd, proto, it->second.address);
      }
    } else {
      string err = "e03ERR";
      sendToClient(sockfd, err, clientAddr);
    }
    break;
  }
  case 'x': {
    lock_guard<mutex> lock(clientsMutex);
    string nick;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
      if (it->second.address.sin_addr.s_addr == clientAddr.sin_addr.s_addr &&
          it->second.address.sin_port == clientAddr.sin_port) {
        nick = it->first;
        m_clients.erase(it);
        break;
      }
    }
    cout << "Cliente " << nick << " desconectado." << endl;
    break;
  }
  default:
    cout << "Tipo de mensaje desconocido: " << type << endl;
    break;
  }
}

int main(void) {
  struct sockaddr_in servaddr, cliaddr;
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  char buffer[BUFFER_SIZE];

  if (sockfd < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  // Permitir reutilizar el puerto
  int opt = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  memset(&servaddr, 0, sizeof(servaddr));
  memset(&cliaddr, 0, sizeof(cliaddr));

  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(PORT);

  if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
    perror("bind failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  cout << "UDP Server listening on port " << PORT << "..." << endl;
  cout << "Buffer size: " << BUFFER_SIZE << " bytes" << endl;

  while (true) {
    socklen_t len = sizeof(cliaddr);
    ssize_t n = recvfrom(sockfd, (char *)buffer, BUFFER_SIZE, MSG_WAITALL,
                         (struct sockaddr *)&cliaddr, &len);

    if (n > 0) {
      buffer[n] = '\0';
      string message(buffer, n);

      // Manejar el mensaje inmediatamente
      handleClientMessage(sockfd, message, cliaddr);
    }
  }

  close(sockfd);
  return 0;
}