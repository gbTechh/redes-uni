#include <arpa/inet.h>
#include <fstream>
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

#define PORT 45000
#define BUFFER_SIZE 65507
#define TIMEOUT_SEC 5

string leerArchivo(string filename) {
  ifstream archivo(filename, ios::binary | ios::ate);
  if (!archivo) {
    cout << "Error: No se pudo abrir el archivo '" << filename << "'" << endl;
    return "";
  }

  streamsize size = archivo.tellg();
  archivo.seekg(0, ios::beg);

  string content(size, '\0');
  if (archivo.read(&content[0], size)) {
    return content;
  }

  return "";
}

void guardarArchivo(const string &filename, const string &data) {
  string nombreDestino;
  size_t puntoPos = filename.find_last_of('.');

  if (puntoPos != string::npos) {
    nombreDestino =
        filename.substr(0, puntoPos) + "-destino" + filename.substr(puntoPos);
  } else {
    nombreDestino = filename + "-destino";
  }

  ofstream archivo(nombreDestino, ios::binary);
  if (archivo) {
    archivo.write(data.c_str(), data.size());
    archivo.close();
    cout << "Archivo guardado como: " << nombreDestino << " (" << data.size()
         << " bytes)" << endl;
  } else {
    cout << "Error al guardar el archivo: " << nombreDestino << endl;
  }
}

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

bool sendWithTimeout(int sockfd, const string &message) {
  sendToServer(sockfd, message);

  // Configurar timeout para la respuesta
  struct timeval tv;
  tv.tv_sec = TIMEOUT_SEC;
  tv.tv_usec = 0;
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  return true;
}

void readThreadFn(int socketConn) {
  char buffer[BUFFER_SIZE];

  while (true) {
    struct sockaddr_in fromAddr;
    socklen_t len = sizeof(fromAddr);

    ssize_t n = recvfrom(socketConn, (char *)buffer, BUFFER_SIZE, MSG_WAITALL,
                         (struct sockaddr *)&fromAddr, &len);

    if (n <= 0) {
      // Timeout o error
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue; // Timeout, continuar esperando
      }
      break;
    }

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
    case 'E': {
      if (content.size() < 15)
        break;

      string errMsg = content.substr(0, 15);
      cout << "\n[Error del servidor] " << errMsg << endl;
      break;
    }
    case 'F': {
      if (content.size() < 2)
        break;

      string lenStr = content.substr(0, 2);
      int senderLen = stoi(lenStr);

      if (content.size() < 2 + senderLen)
        break;
      string sender = content.substr(2, senderLen);

      if (content.size() < 2 + senderLen + 3)
        break;
      string sizeFilename = content.substr(2 + senderLen, 3);
      int filenameLen = stoi(sizeFilename);

      if (content.size() < 2 + senderLen + 3 + filenameLen)
        break;
      string filename = content.substr(2 + senderLen + 3, filenameLen);

      if (content.size() < 2 + senderLen + 3 + filenameLen + 10)
        break;
      string sizeFileData = content.substr(2 + senderLen + 3 + filenameLen, 10);
      long fileDataLen = stol(sizeFileData);

      if (content.size() < 2 + senderLen + 3 + filenameLen + 10 + fileDataLen)
        break;
      string fileData =
          content.substr(2 + senderLen + 3 + filenameLen + 10, fileDataLen);

      cout << "\n[Archivo recibido de " << sender << "] " << filename << " ("
           << fileDataLen << " bytes)" << endl;
      guardarArchivo(filename, fileData);
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
  do {
    cout << "\n--- MENU PRINCIPAL ---\n";
    cout << "1. Enviar mensaje a un cliente (privado)\n";
    cout << "2. Enviar mensaje a todos los clientes (broadcast)\n";
    cout << "3. Listar todos los clientes conectados\n";
    cout << "4. Enviar un archivo\n";
    cout << "5. Salir (Cerrar conexion)\n";
    cout << "Seleccione una opcion: ";
    if (!(cin >> opt)) {
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
      string dest, filename;
      cout << "Ingrese el nickname del destinatario: ";
      getline(cin, dest);
      cout << "Escriba el nombre del archivo: ";
      getline(cin, filename);

      string fileData = leerArchivo(filename);
      if (fileData.empty()) {
        cout << "No se pudo leer el archivo o está vacío." << endl;
        continue;
      }

      string payload = "f" + padNumber((int)dest.size(), 2) + dest +
                       padNumber((int)filename.size(), 3) + filename +
                       padNumber((int)fileData.size(), 10) + fileData;

      cout << "Tamaño del payload: " << payload.size() << " bytes" << endl;

      // Verificar si el archivo es demasiado grande para UDP
      if (payload.size() > BUFFER_SIZE) {
        cout << "Error: Archivo demasiado grande para UDP (" << payload.size()
             << " bytes). Límite: " << BUFFER_SIZE << " bytes." << endl;
        continue;
      }

      cout << "Enviando archivo..." << endl;
      sendToServer(SocketCli, payload);

    } else if (opt == 5) {
      string payload = "x";
      sendToServer(SocketCli, payload);
      cout << "Desconectando...\n";
      break;
    } else {
      cout << "Opcion no valida\n";
    }

  } while (opt != 5);

  // Pequeña pausa antes de cerrar
  this_thread::sleep_for(chrono::seconds(1));
  close(SocketCli);
  if (reader.joinable())
    reader.join();

  cout << "Cliente finalizado.\n";
  return 0;
}