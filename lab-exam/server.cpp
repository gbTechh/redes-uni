/* Server code in C */

#include <arpa/inet.h>
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
#include <fstream>
#include <ctime>

using namespace std;

map<string, int> m_clients;
mutex clientsMutex;

string padNumber(int num, int width) {
  string s = to_string(num);
  if ((int)s.size() < width)
    s = string(width - s.size(), '0') + s;
  return s;
}

// Función para guardar mensajes en archivo binario
void guardarMensajeHistorial(const string& tipo, const string& remitente, 
                            const string& destinatario, const string& mensaje) {
    ofstream archivo("historial_chat.bin", ios::binary | ios::app);
    
    if (archivo) {
        // Timestamp (fecha/hora actual)
        time_t ahora = time(nullptr);
        archivo.write((char*)&ahora, sizeof(ahora));
        
        // Tipo de mensaje (1 byte)
        char tipoChar = tipo[0];
        archivo.write(&tipoChar, 1);
        
        // Longitud y remitente
        uint16_t longRemitente = remitente.size();
        archivo.write((char*)&longRemitente, sizeof(longRemitente));
        archivo.write(remitente.c_str(), longRemitente);
        
        // Longitud y destinatario
        uint16_t longDestinatario = destinatario.size();
        archivo.write((char*)&longDestinatario, sizeof(longDestinatario));
        archivo.write(destinatario.c_str(), longDestinatario);
        
        // Longitud y mensaje
        uint32_t longMensaje = mensaje.size();
        archivo.write((char*)&longMensaje, sizeof(longMensaje));
        archivo.write(mensaje.c_str(), longMensaje);
    }
}

// Función para leer el historial desde el servidor
void leerHistorialServidor() {
    ifstream archivo("historial_chat.bin", ios::binary);
    
    if (!archivo) {
        cout << "No hay historial disponible" << endl;
        return;
    }
    
    cout << "\n=== HISTORIAL DEL CHAT (SERVER) ===" << endl;
    
    while (archivo) {
        time_t fecha;
        archivo.read((char*)&fecha, sizeof(fecha));
        if (!archivo) break;
        
        char tipo;
        archivo.read(&tipo, 1);
        
        uint16_t longRemitente;
        archivo.read((char*)&longRemitente, sizeof(longRemitente));
        string remitente(longRemitente, ' ');
        archivo.read(&remitente[0], longRemitente);
        
        uint16_t longDestinatario;
        archivo.read((char*)&longDestinatario, sizeof(longDestinatario));
        string destinatario(longDestinatario, ' ');
        archivo.read(&destinatario[0], longDestinatario);
        
        uint32_t longMensaje;
        archivo.read((char*)&longMensaje, sizeof(longMensaje));
        string mensaje(longMensaje, ' ');
        archivo.read(&mensaje[0], longMensaje);
        
        // Formatear la fecha
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&fecha));
        
        if (tipo == 'M') {
            cout << "[" << buffer << "] BROADCAST de " << remitente << ": " << mensaje << endl;
        } else if (tipo == 'T') {
            cout << "[" << buffer << "] PRIVADO de " << remitente << " para " << destinatario << ": " << mensaje << endl;
        }
    }
    cout << "=== FIN DEL HISTORIAL ===" << endl;
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

      // GUARDAR EN HISTORIAL
      guardarMensajeHistorial("M", senderNick, "Todos", msg);

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

      // GUARDAR EN HISTORIAL
      guardarMensajeHistorial("T", senderNick, destNick, msg);

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
    case 'h': { // 'h' para historial
        cout << "Cliente solicitó historial del chat" << endl;
        
        // Enviar historial en formato de texto simple
        ifstream archivo("historial_chat.bin", ios::binary);
        string historialCompleto;
        
        if (archivo) {
            archivo.seekg(0, ios::end);
            size_t fileSize = archivo.tellg();
            archivo.seekg(0, ios::beg);
            
            historialCompleto.resize(fileSize);
            archivo.read(&historialCompleto[0], fileSize);
        }
        
        if (historialCompleto.empty()) {
            string respuesta = "H000000No hay historial disponible";
            write(socketConn, respuesta.c_str(), respuesta.size());
        } else {
            string respuesta = "H" + padNumber(historialCompleto.size(), 6) + historialCompleto;
            write(socketConn, respuesta.c_str(), respuesta.size());
        }
        break;
    }
    case 'x': {
      string nick;
      {
        lock_guard<mutex> lock(clientsMutex);
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
          if (it->second == socketConn) {
            nick = it->first;
            m_clients.erase(it);
            break;
          }
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

  cout << "Servidor iniciado en puerto 45000" << endl;
  cout << "Comandos del servidor:" << endl;
  cout << "  - 'historial': Ver historial del chat" << endl;
  cout << "  - 'quit': Salir del servidor" << endl;

  // Hilo para comandos del servidor
  thread serverThread([]() {
      string comando;
      while (true) {
          cout << "\nServidor> ";
          getline(cin, comando);
          
          if (comando == "historial") {
              leerHistorialServidor();
          } else if (comando == "quit") {
              cout << "Apagando servidor..." << endl;
              exit(0);
          } else if (!comando.empty()) {
              cout << "Comando no reconocido: " << comando << endl;
          }
      }
  });
  serverThread.detach();

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