// Client UDP Version - Go-Back-N (GBN) Implementation
#include <arpa/inet.h>
#include <chrono>
#include <iostream>
#include <map>
#include <mutex> // Nuevo: para sincronización de hilos
#include <netinet/in.h>
#include <queue> // Nuevo: para el buffer de mensajes de la aplicación
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

#define WINDOW_SIZE 4
#define TIMEOUT_MS 500
int globalSeqNum = 0;
int baseSeqNum = 0;
bool waitingForACK = false;

mutex sendMutex;
queue<string> sendBuffer;
map<int, string> packetBuffer;

map<int, chrono::steady_clock::time_point> packetSentTime;
bool running = true;

struct Packet {
  int seqnum;
  int acknum;
  int checksum;
  string payload;
};

string serializePacket(const Packet &pkt) {
  string result;
  result += to_string(pkt.seqnum) + "|";
  result += to_string(pkt.acknum) + "|";
  result += to_string(pkt.checksum) + "|";
  result += pkt.payload;
  return result;
}

Packet deserializePacket(const string &data) {
  Packet pkt;
  size_t pos1 = data.find('|');
  size_t pos2 = data.find('|', pos1 + 1);
  size_t pos3 = data.find('|', pos2 + 1);

  if (pos1 == string::npos || pos2 == string::npos || pos3 == string::npos) {
    pkt.seqnum = -1;
    return pkt;
  }

  try {
    pkt.seqnum = stoi(data.substr(0, pos1));
    pkt.acknum = stoi(data.substr(pos1 + 1, pos2 - pos1 - 1));
    pkt.checksum = stoi(data.substr(pos2 + 1, pos3 - pos2 - 1));
    pkt.payload = data.substr(pos3 + 1);
  } catch (...) {
    pkt.seqnum = -1;
  }
  return pkt;
}

int calcularChecksum(const Packet &pkt) {
  int suma = pkt.seqnum + pkt.acknum;
  for (char c : pkt.payload) {
    suma += (unsigned char)c;
  }
  int checksum = 255 - (suma % 256);
  return checksum;
}

void makeAndPrintPlays(string table) {
  vector<string> v_plays(9);
  for (size_t i = 0; i < table.size(); i++) {
    v_plays[i] = table[i];
  }
  auto printPlays = [](vector<string> &vp) {
    for (size_t j = 0; j < vp.size(); j++) {
      if (j % 3 == 0)
        cout << "\n";
      cout << vp[j] << "|";
    }
    cout << "\n";
  };
  printPlays(v_plays);
}

string removePadding(const string &message) {
  size_t pos = message.find('#');
  if (pos != string::npos) {
    return message.substr(0, pos);
  }
  return message;
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

void queueMessage(string message) {
  lock_guard<mutex> lock(sendMutex);
  sendBuffer.push(message);
  cout << "Comando en cola: [" << message << "]\n";
}

void senderThreadFn(int sockfd) {
  while (running) {
    {
      lock_guard<mutex> lock(sendMutex);

      while (!sendBuffer.empty() && (globalSeqNum < baseSeqNum + WINDOW_SIZE)) {
        string message = sendBuffer.front();
        sendBuffer.pop();

        char messageType = message[0];
        string messageData = message.substr(1);
        int maxDataPerFragment = BUFFER_SIZE - 1 - 7;
        int totalFragments =
            (messageData.size() + maxDataPerFragment - 1) / maxDataPerFragment;

        Packet pkt;
        pkt.seqnum = globalSeqNum;
        pkt.acknum = 0;
        pkt.payload = message;
        pkt.checksum = calcularChecksum(pkt);

        string serialized = serializePacket(pkt);

        serialized.resize(BUFFER_SIZE, '#');

        packetBuffer[globalSeqNum] = serialized;
        packetSentTime[globalSeqNum] = chrono::steady_clock::now();

        sendto(sockfd, serialized.c_str(), serialized.size(), 0,
               (const struct sockaddr *)&servaddr, sizeof(servaddr));

        cout << "[Sender GBN] Enviado PKT #" << globalSeqNum
             << " (Tipo: " << message[0] << ")" << endl;
        globalSeqNum++;

        if (baseSeqNum == globalSeqNum - 1) {
          waitingForACK = true;
        }
      }

      if (waitingForACK && !packetBuffer.empty()) {
        auto now = chrono::steady_clock::now();
        auto sentTime = packetSentTime[baseSeqNum];
        auto elapsed =
            chrono::duration_cast<chrono::milliseconds>(now - sentTime);

        if (elapsed.count() > TIMEOUT_MS) {
          cout << "[Sender GBN] Timeout en PKT #" << baseSeqNum
               << ". Retransmitiendo desde BASE..." << endl;

          for (int i = baseSeqNum; i < globalSeqNum; ++i) {
            string serialized = packetBuffer[i];
            sendto(sockfd, serialized.c_str(), serialized.size(), 0,
                   (const struct sockaddr *)&servaddr, sizeof(servaddr));
            cout << "[Sender GBN] Reenviando PKT #" << i << endl;
          }

          packetSentTime[baseSeqNum] = chrono::steady_clock::now();
        }
      }
    }
    this_thread::sleep_for(chrono::milliseconds(10));
  }
}

// Hilo para leer respuestas y ACKs del servidor
void readThreadFn(int socketConn) {
  char buffer[BUFFER_SIZE];
  struct sockaddr_in fromAddr;
  socklen_t fromLen = sizeof(fromAddr);

  while (running) {
    ssize_t n = recvfrom(socketConn, buffer, BUFFER_SIZE - 1, 0,
                         (struct sockaddr *)&fromAddr, &fromLen);
    if (n <= 0) {
      break;
    }

    buffer[n] = '\0';
    string message(buffer, n);
    if (message.empty())
      continue;

    Packet receivedPkt = deserializePacket(message);

    if (receivedPkt.seqnum == -1)
      continue;

    string payloadWithoutPadding = removePadding(receivedPkt.payload);
    Packet tempPkt = receivedPkt;
    tempPkt.payload = payloadWithoutPadding;

    if (calcularChecksum(tempPkt) != receivedPkt.checksum) {
      cout << "ERROR: Checksum incorrecto. Descartando paquete." << endl;
      continue;
    }

    if (receivedPkt.acknum > 0 || payloadWithoutPadding == "ACK") {
      lock_guard<mutex> lock(sendMutex);
      // El ACK recibido es el número de secuencia *siguiente* esperado (N)
      int newBase = receivedPkt.acknum;

      cout << "[Reader GBN] Recibido ACK #" << newBase
           << ". Base anterior: " << baseSeqNum << endl;

      if (newBase > baseSeqNum) {
        // Mover la ventana: eliminar todos los paquetes hasta newBase - 1
        for (int i = baseSeqNum; i < newBase; ++i) {
          packetBuffer.erase(i);
          packetSentTime.erase(i);
          cout << "[Reader GBN] Paquete #" << i << " reconocido y eliminado."
               << endl;
        }
        baseSeqNum = newBase;

        if (baseSeqNum == globalSeqNum) {
          waitingForACK = false;
          cout << "[Reader GBN] Ventana vacía. Temporizador detenido." << endl;
        } else {

          packetSentTime[baseSeqNum] = chrono::steady_clock::now();
          cout << "[Reader GBN] Temporizador reiniciado para paquete #"
               << baseSeqNum << endl;
        }
      }
      continue;
    }

    cout << "PAYLOAD READTHREAD: " << payloadWithoutPadding << endl;

    string payload = payloadWithoutPadding;

    if (payload.empty())
      continue;

    char type = payload[0];

    switch (type) {
    case 'T': {

      if (payload.size() < 3)
        continue;

      string lenNickStr = payload.substr(1, 2);
      int lenNick = stoi(lenNickStr);

      if (payload.size() < 3 + lenNick)
        continue;
      string sender = payload.substr(3, lenNick);

      if (payload.size() < 3 + lenNick + 3)
        continue;
      string lenMsgStr = payload.substr(3 + lenNick, 3);
      int lenMsg = stoi(lenMsgStr);

      if (payload.size() < 3 + lenNick + 3 + lenMsg)
        continue;
      string msg = payload.substr(3 + lenNick + 3, lenMsg);

      cout << "\n[Privado de " << sender << "] " << msg << endl;
      break;
    }

    case 'M': {

      if (payload.size() < 3)
        continue;

      string lenNickStr = payload.substr(1, 2);
      int lenNick = stoi(lenNickStr);

      if (payload.size() < 3 + lenNick)
        continue;
      string sender = payload.substr(3, lenNick);

      if (payload.size() < 3 + lenNick + 3)
        continue;
      string lenMsgStr = payload.substr(3 + lenNick, 3);
      int lenMsg = stoi(lenMsgStr);

      if (payload.size() < 3 + lenNick + 3 + lenMsg)
        continue;
      string msg = payload.substr(3 + lenNick + 3, lenMsg);

      cout << "\n[Broadcast de " << sender << "] " << msg << endl;
      break;
    }

    case 'V': {

      if (payload.size() < 2)
        continue;
      inGame = true;
      string play = payload.substr(1, 1);
      cout << "Te toca jugar, tu jugada es [" << play << "]:\n";
      jugada = play;
      break;
    }

    case 'v': {

      if (payload.size() < 10)
        continue;
      string tablero = payload.substr(1, 9);
      cout << "[TABLERO]\n";
      makeAndPrintPlays(tablero);
      break;
    }

    case 'E': {

      if (payload.size() < 16)
        continue;
      string errMsg = payload.substr(1, 15);
      cout << "\n[Error del servidor] " << errMsg << endl;
      break;
    }

    case 'L': {

      if (payload.size() < 3)
        continue;

      string countStr = payload.substr(1, 2);
      int numUsers = stoi(countStr);

      cout << "\n[Usuarios conectados] ";

      size_t pos = 3;
      for (int i = 0; i < numUsers; ++i) {
        if (pos + 2 > payload.size())
          break;

        string lenStr = payload.substr(pos, 2);
        int len = stoi(lenStr);
        pos += 2;

        if (pos + len > payload.size())
          break;

        string nick = payload.substr(pos, len);
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
  queueMessage(reg);

  thread sender(senderThreadFn, SocketCli);
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
      queueMessage(payloadList);

      cout << "Comando en cola (Lista previa): [" << payloadList << "]\n";

      string dest, msg;
      cout << "Ingrese el nickname del destinatario: ";
      getline(cin, dest);
      cout << "Escriba el mensaje: ";
      getline(cin, msg);
      string payload = "t" + padNumber((int)dest.size(), 2) + dest +
                       padNumber((int)msg.size(), 3) + msg;

      cout << "Mensaje privado en cola: [" << payload << "]\n";
      queueMessage(payload);

    } else if (opt == 2) {
      string msg;
      cout << "Escriba el mensaje para todos: ";
      getline(cin, msg);
      string payload = "m" + padNumber((int)msg.size(), 3) + msg;

      cout << "Broadcast en cola: [" << payload << "]\n";
      queueMessage(payload);

    } else if (opt == 3) {
      string payload = "l";

      cout << "Listar en cola: [" << payload << "]\n";
      queueMessage(payload);

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

          cout << "Enviando jugada en cola: [" << payload << "]\n";
          queueMessage(payload);
          break;
        }
      } else {
        string payload = "p";

        cout << "Solicitar partida en cola: [" << payload << "]\n";
        queueMessage(payload);
      }

    } else if (opt == 5) {
      string payload = "x";

      cout << "Solicitar salida en cola: [" << payload << "]\n";
      queueMessage(payload);
      break;
    } else {
      cout << "Opcion no valida\n";
    }
  }

  running = false;
  close(SocketCli);
  if (reader.joinable())
    reader.join();
  if (sender.joinable())
    sender.join();

  cout << "Cliente finalizado.\n";
  return 0;
}