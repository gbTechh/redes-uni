// Client UDP Version - CORREGIDO con lectura UDP
#include <arpa/inet.h>
#include <chrono>
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
#include <vector>

using namespace std;

#define PORT 45000
#define BUFFER_SIZE 777

void printPlays(vector<string> &v_plays) {
  for (size_t i = 0; i < v_plays.size(); i++) {
    if (i % 3 == 0)
      cout << "\n";
    cout << v_plays[i] << "|";
  }
  cout << "\n";
}

void makeAndPrintPlays(string table) {
  vector<string> v_plays(9);
  for (size_t i = 0; i < table.size(); i++) {
    v_plays[i] = table[i];
  }
  printPlays(v_plays);
}

string jugada;
bool inGame = false;

string padNumber(int num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

struct sockaddr_in servaddr;

void sendToServer(int sockfd, string message) {
  if (message.size() > 777) {
    char messageType = message[0];
    string messageData = message.substr(1);

    int maxDataPerFragment = 777 - 1 - 7;
    int totalFragments =
        (messageData.size() + maxDataPerFragment - 1) / maxDataPerFragment;

    for (int i = 0; i < totalFragments; i++) {
      int start = i * maxDataPerFragment;
      int size = min(maxDataPerFragment, (int)messageData.size() - start);
      string fragmentData = messageData.substr(start, size);

      string fragHeader =
          padNumber(i + 1, 3) + "|" + padNumber(totalFragments, 3); // 003|003

      string fragmentMessage =
          string(1, messageType) + fragHeader + fragmentData;
      fragmentMessage.resize(777, '#');

      sendto(sockfd, fragmentMessage.c_str(), 777, 0,
             (const struct sockaddr *)&servaddr, sizeof(servaddr));
    }
  } else {
    string msg = message;
    msg.resize(777, '#');
    sendto(sockfd, msg.c_str(), msg.size(), 0,
           (const struct sockaddr *)&servaddr, sizeof(servaddr));
  }
}

void readThreadFn(int socketConn) {
  char buffer[BUFFER_SIZE];
  struct sockaddr_in fromAddr;
  socklen_t fromLen = sizeof(fromAddr);

  while (true) {
    ssize_t n = recvfrom(socketConn, buffer, BUFFER_SIZE - 1, 0,
                         (struct sockaddr *)&fromAddr, &fromLen);
    if (n <= 0) {
      break;
    }

    buffer[n] = '\0';
    string message(buffer, n);

    if (message.empty())
      continue;

    char type = message[0];

    switch (type) {
    case 'T': {
      if (message.size() < 3)
        continue;

      string lenNickStr = message.substr(1, 2);
      int lenNick = stoi(lenNickStr);

      if (message.size() < 3 + lenNick)
        continue;
      string sender = message.substr(3, lenNick);

      if (message.size() < 3 + lenNick + 3)
        continue;
      string lenMsgStr = message.substr(3 + lenNick, 3);
      int lenMsg = stoi(lenMsgStr);

      if (message.size() < 3 + lenNick + 3 + lenMsg)
        continue;
      string msg = message.substr(3 + lenNick + 3, lenMsg);

      cout << "\n[Privado de " << sender << "] " << msg << endl;
      break;
    }

    case 'M': {
      if (message.size() < 3)
        continue;

      string lenNickStr = message.substr(1, 2);
      int lenNick = stoi(lenNickStr);

      if (message.size() < 3 + lenNick)
        continue;
      string sender = message.substr(3, lenNick);

      if (message.size() < 3 + lenNick + 3)
        continue;
      string lenMsgStr = message.substr(3 + lenNick, 3);
      int lenMsg = stoi(lenMsgStr);

      if (message.size() < 3 + lenNick + 3 + lenMsg)
        continue;
      string msg = message.substr(3 + lenNick + 3, lenMsg);

      cout << "\n[Broadcast de " << sender << "] " << msg << endl;
      break;
    }

    case 'V': {
      if (message.size() < 2)
        continue;

      inGame = true;
      string play = message.substr(1, 1);
      cout << "Te toca jugar, tu jugada es [" << play << "]:\n";
      jugada = play;
      break;
    }

    case 'v': {
      if (message.size() < 10)
        continue;

      string tablero = message.substr(1, 9);
      cout << "[TABLERO]\n";
      makeAndPrintPlays(tablero);
      break;
    }

    case 'E': {
      if (message.size() < 16)
        continue;

      string errMsg = message.substr(1, 15);
      cout << "\n[Error del servidor] " << errMsg << endl;
      break;
    }

    case 'L': {
      if (message.size() < 3)
        continue;

      string countStr = message.substr(1, 2);
      int numUsers = stoi(countStr);

      cout << "\n[Usuarios conectados] ";

      size_t pos = 3;
      for (int i = 0; i < numUsers; ++i) {
        if (pos + 2 > message.size())
          break;

        string lenStr = message.substr(pos, 2);
        int len = stoi(lenStr);
        pos += 2;

        if (pos + len > message.size())
          break;

        string nick = message.substr(pos, len);
        pos += len;

        cout << nick << " ";
      }
      cout << endl;
      break;
    }

    default: {
      cout << "\n[Server err] Tipo desconocido: " << type << endl;
      break;
    }
    }
  }
  cout << "Lectura: conexión cerrada por el servidor (o error).\n";
}

int main() {
  const char *serverIP = "127.0.0.1";

  int SocketCli = socket(AF_INET, SOCK_DGRAM, 0);
  if (SocketCli < 0) {
    perror("cannot create socket");
    return 1;
  }

  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(PORT);

  if (inet_pton(AF_INET, serverIP, &servaddr.sin_addr) <= 0) {
    perror("inet_pton failed");
    close(SocketCli);
    return 1;
  }

  string nickname;
  cout << "Escriba su nickname: ";
  getline(cin, nickname);
  if (nickname.empty())
    nickname = "user";

  string reg = "n" + padNumber((int)nickname.size(), 2) + nickname;
  sendToServer(SocketCli, reg);
  cout << "[" << reg << "]\n";

  thread reader(readThreadFn, SocketCli);

  int opt = 0;
  while (opt != 5) {

    cout << "\n--- MENU PRINCIPAL ---\n";
    cout << "1. Enviar mensaje a un cliente (privado)\n";
    cout << "2. Enviar mensaje a todos los clientes (broadcast)\n";
    cout << "3. Listar todos los clientes conectados\n";
    cout << "4. Jugar Tic Tac Toe\n";
    cout << "5. Salir (Cerrar conexion)\n";
    cout << "Seleccione una opcion: ";
    if (!(cin >> opt) && !inGame) {
      cin.clear();
      string dummy;
      getline(cin, dummy);
      opt = 0;
    }
    cin.ignore(); // limpiar \n restante

    if (opt == 1) {
      string payloadList = "l";
      sendToServer(SocketCli, payloadList);
      cout << "[" << payloadList << "]\n";
      string dest, msg;
      cout << "Ingrese el nickname del destinatario: ";
      getline(cin, dest);
      cout << "Escriba el mensaje: ";
      getline(cin, msg);
      string payload = "t" + padNumber((int)dest.size(), 2) + dest +
                       padNumber((int)msg.size(), 3) + msg;
      cout << "[" << payload << "]\n";
      sendToServer(SocketCli, payload);

    } else if (opt == 2) {
      string msg;
      cout << "Escriba el mensaje para todos: ";
      getline(cin, msg);
      string payload = "m" + padNumber((int)msg.size(), 3) + msg;
      cout << "[" << payload << "]\n";
      sendToServer(SocketCli, payload);

    } else if (opt == 3) {
      string payload = "l";
      sendToServer(SocketCli, payload);
      cout << "[" << payload << "]\n";

    } else if (opt == 4) {

      if (inGame) {
        while (true) {

          string pos;

          cout << "\nSelecciona tu posicion [1-9]: ";
          getline(cin, pos);

          // Validar entrada
          if (pos.length() != 1 || pos[0] < '1' || pos[0] > '9') {
            cout << "Posición inválida. Debe ser un número del 1 al 9.\n";
            continue;
          }
          string payload = string("w") + jugada + pos;
          cout << "Enviando jugada: [" << payload << "]\n";
          sendToServer(SocketCli, payload);
          break;
        }
      } else {
        string payload = "p";
        cout << "[" << payload << "]\n";
        sendToServer(SocketCli, payload);
      }

    } else if (opt == 7) {
      string pos, play;
      cout << "\nSelecciona tu posicion [1-9]: ";
      getline(cin, pos);

      // Validar entrada
      if (pos.length() != 1 || pos[0] < '1' || pos[0] > '9') {
        cout << "Posición inválida. Debe ser un número del 1 al 9.\n";
        continue;
      }

      cout << "\nSelecciona tu jugada [x/o]: ";
      getline(cin, play);

      // Validar jugada
      if (play.length() != 1 || (play[0] != 'x' && play[0] != 'o')) {
        cout << "Jugada inválida. Debe ser 'x' u 'o'.\n";
        continue;
      }
      string payload = "w" + pos + play;
      cout << "Enviando jugada: [" << payload << "]\n";
      sendToServer(SocketCli, payload);

    } else if (opt == 5) {
      // mandar 'x' para indicar salida al servidor
      string payload = "x";
      sendToServer(SocketCli, payload);
      cout << "[" << payload << "]\n";
      break;
    } else {
      cout << "Opcion no valida\n";
    }
  }

  // cerrar y esperar hilo lector
  close(SocketCli);
  if (reader.joinable())
    reader.join();

  cout << "Cliente finalizado.\n";
  return 0;
}