// Client UDP Version
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
#include <vector>

using namespace std;

#define PORT 45000
#define BUFFER_SIZE 1024

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

void sendToServer(int sockfd, const string &message) {
  sendto(sockfd, message.c_str(), message.size(), 0,
         (const struct sockaddr *)&servaddr, sizeof(servaddr));
}

void readThreadFn(int socketConn) {
  char buffer[BUFFER_SIZE];

  while (true) {
    struct sockaddr_in fromAddr;
    socklen_t len = sizeof(fromAddr);

    ssize_t n = recvfrom(socketConn, (char *)buffer, BUFFER_SIZE, MSG_WAITALL,
                         (struct sockaddr *)&fromAddr, &len);

    if (n <= 0)
      break;

    buffer[n] = '\0';
    string message(buffer, n);

    char type = message[0];
    string content = message.substr(1);

    switch (type) {
    case 'T': {
      if (content.size() < 2)
        break;

      string lenNickStr = content.substr(0, 2);
      int lenNick = stoi(lenNickStr);

      if (content.size() < 2 + lenNick)
        break;
      string sender = content.substr(2, lenNick);

      if (content.size() < 2 + lenNick + 3)
        break;
      string lenMsgStr = content.substr(2 + lenNick, 3);
      int lenMsg = stoi(lenMsgStr);

      if (content.size() < 2 + lenNick + 3 + lenMsg)
        break;
      string msg = content.substr(2 + lenNick + 3, lenMsg);

      cout << "\n[Privado de " << sender << "] " << msg << endl;
      break;
    }
    case 'M': {
      if (content.size() < 2)
        break;

      string lenNickStr = content.substr(0, 2);
      int lenNick = stoi(lenNickStr);

      if (content.size() < 2 + lenNick)
        break;
      string sender = content.substr(2, lenNick);

      if (content.size() < 2 + lenNick + 3)
        break;
      string lenMsgStr = content.substr(2 + lenNick, 3);
      int lenMsg = stoi(lenMsgStr);

      if (content.size() < 2 + lenNick + 3 + lenMsg)
        break;
      string msg = content.substr(2 + lenNick + 3, lenMsg);

      cout << "\n[Broadcast de " << sender << "] " << msg << endl;
      break;
    }
    case 'V': {
      if (content.size() < 1)
        break;

      inGame = true;
      string play = content.substr(0, 1);
      cout << "Te toca jugar, tu jugada es [" << play << "]:\n";
      jugada = play;
      break;
    }
    case 'v': {
      if (content.size() < 9)
        break;

      string tablero = content.substr(0, 9);
      cout << "[TABLERO]\n";
      makeAndPrintPlays(tablero);
      break;
    }
    case 'E': {
      if (content.size() < 15)
        break;

      string errMsg = content.substr(0, 15);
      cout << "\n[Error del servidor] " << errMsg << endl;
      break;
    }
    case 'L': {
      if (content.size() < 2)
        break;

      string countStr = content.substr(0, 2);
      int numUsers = stoi(countStr);

      cout << "\n[Usuarios conectados] ";
      int pos = 2;
      for (int i = 0; i < numUsers; ++i) {
        if (content.size() < pos + 2)
          break;

        string lenStr = content.substr(pos, 2);
        pos += 2;
        int len = stoi(lenStr);

        if (content.size() < pos + len)
          break;
        string nick = content.substr(pos, len);
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
  cout << "Conexión con el servidor perdida." << endl;
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
  cout << "Registro enviado: [" << reg << "]\n";

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
    cin.ignore();

    if (opt == 1) {
      string payloadList = "l";
      sendToServer(SocketCli, payloadList);
      cout << "Solicitando lista de usuarios...\n";

      // Pequeña pausa para recibir la lista
      this_thread::sleep_for(chrono::milliseconds(500));

      string dest, msg;
      cout << "Ingrese el nickname del destinatario: ";
      getline(cin, dest);
      cout << "Escriba el mensaje: ";
      getline(cin, msg);
      string payload = "t" + padNumber((int)dest.size(), 2) + dest +
                       padNumber((int)msg.size(), 3) + msg;
      cout << "Enviando mensaje privado: [" << payload << "]\n";
      sendToServer(SocketCli, payload);

    } else if (opt == 2) {
      string msg;
      cout << "Escriba el mensaje para todos: ";
      getline(cin, msg);
      string payload = "m" + padNumber((int)msg.size(), 3) + msg;
      cout << "Enviando broadcast: [" << payload << "]\n";
      sendToServer(SocketCli, payload);

    } else if (opt == 3) {
      string payload = "l";
      sendToServer(SocketCli, payload);
      cout << "Solicitando lista de usuarios...\n";

    } else if (opt == 4) {
      if (inGame) {
        while (true) {
          string pos;
          cout << "\nSelecciona tu posicion [1-9]: ";
          getline(cin, pos);

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
        cout << "Uniéndose al juego... [" << payload << "]\n";
        sendToServer(SocketCli, payload);
      }

    } else if (opt == 5) {
      string payload = "x";
      sendToServer(SocketCli, payload);
      cout << "Desconectando... [" << payload << "]\n";
      break;
    } else {
      cout << "Opcion no valida\n";
    }
  }

  // Pequeña pausa antes de cerrar
  this_thread::sleep_for(chrono::seconds(1));
  close(SocketCli);
  if (reader.joinable())
    reader.join();

  cout << "Cliente finalizado.\n";
  return 0;
}