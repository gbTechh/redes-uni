#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

const int PORT = 9999;
const int PROTOCOL_SIZE = 777;
const int BUFFER_SIZE = 777 + 1;

int extractSeqNum(char *datagram) {
  char seqStr[11];
  strncpy(seqStr, datagram + 1, 10);
  seqStr[10] = '\0';
  return stoi(seqStr);
}

int makeHash(char *datagram, int size) {
  int sum = 0;
  for (int i = 0; i < size; i++) {
    if (i != 11) {
      sum += static_cast<int>(datagram[i]);
    }
  }
  return sum % 6;
}

void enviarACK(int sockfd, int seqNum, struct sockaddr_in cliaddr, int len) {
  char ackDatagram[PROTOCOL_SIZE + 1];
  memset(ackDatagram, 0, PROTOCOL_SIZE + 1);

  ackDatagram[0] = 'A';

  char buf[11];
  sprintf(buf, "%010d", seqNum);
  strncpy(ackDatagram + 1, buf, 10);

  for (int p = 12; p < 777; p++) {
    ackDatagram[p] = '#';
  }
  ackDatagram[777] = '\0';

  ackDatagram[11] = '0';
  int ackChecksum = makeHash(ackDatagram, PROTOCOL_SIZE);
  ackDatagram[11] = '0' + ackChecksum;

  sendto(sockfd, ackDatagram, PROTOCOL_SIZE, MSG_CONFIRM,
         (struct sockaddr *)&cliaddr, len);
}

void enviarNACK(int sockfd, int seqNum, struct sockaddr_in cliaddr, int len) {
  char nackDatagram[PROTOCOL_SIZE + 1];
  memset(nackDatagram, 0, PROTOCOL_SIZE + 1);

  nackDatagram[0] = 'N';
  char buf[11];
  sprintf(buf, "%010d", seqNum);
  strncpy(nackDatagram + 1, buf, 10);

  for (int p = 12; p < 777; p++) {
    nackDatagram[p] = '#';
  }
  nackDatagram[777] = '\0';

  nackDatagram[11] = '0';
  int nackChecksum = makeHash(nackDatagram, PROTOCOL_SIZE);
  nackDatagram[11] = '0' + nackChecksum;

  sendto(sockfd, nackDatagram, PROTOCOL_SIZE, MSG_CONFIRM,
         (struct sockaddr *)&cliaddr, len);
}

string extraerDatos(char *datagram) {
  string datos = string(datagram + 12);
  size_t paddingPos = datos.find('#');
  if (paddingPos != string::npos) {
    datos = datos.substr(0, paddingPos);
  }
  return datos;
}

bool servidorEnviarYConfirmar(int sockfd, char *datagram, int seqNum,
                              struct sockaddr_in cliaddr, int addrlen) {
  int intentos = 0;
  const int MAX_INTENTOS = 3;
  char buffer[BUFFER_SIZE];
  int len = sizeof(cliaddr);

  while (intentos < MAX_INTENTOS) {
    sendto(sockfd, datagram, PROTOCOL_SIZE, MSG_CONFIRM,
           (struct sockaddr *)&cliaddr, addrlen);

    cout << "Servidor envió seq " << seqNum << " (intento " << (intentos + 1)
         << ")" << endl;

    int n = recvfrom(sockfd, buffer, PROTOCOL_SIZE, MSG_DONTWAIT,
                     (struct sockaddr *)&cliaddr, (socklen_t *)&len);

    if (n > 0) {
      buffer[n] = '\0';
      char tipo = buffer[0];
      int seqRecibido = extractSeqNum(buffer);

      if (tipo == 'A' && seqRecibido == seqNum) {
        cout << "ACK recibido del cliente para seq " << seqNum << endl;
        return true;
      } else if (tipo == 'N' && seqRecibido == seqNum) {
        cout << "NACK del cliente para seq " << seqNum << ", reintento "
             << (intentos + 1) << endl;
        break;
      } else if (tipo == 'D') {
        // Mensaje nuevo del cliente
        string datos = extraerDatos(buffer);
        cout << "\nCliente dice: " << datos << endl;
        // Procesar checksum y responder
        int hashRecibido = buffer[11] - '0';
        int hashCalculado = makeHash(buffer, n);
        int clienteSeq = extractSeqNum(buffer);
        if (hashRecibido == hashCalculado) {
          enviarACK(sockfd, clienteSeq, cliaddr, len);
        } else {
          enviarNACK(sockfd, clienteSeq, cliaddr, len);
        }
      }
    }

    intentos++;
  }
  return false;
}

int main() {
  int sockfd;
  char buffer[BUFFER_SIZE];
  struct sockaddr_in servaddr, cliaddr;

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  memset(&servaddr, 0, sizeof(servaddr));
  memset(&cliaddr, 0, sizeof(cliaddr));

  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(PORT);

  if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  int len, n;
  len = sizeof(cliaddr);

  cout << "Servidor UDP RDT iniciado en puerto " << PORT << endl;
  cout << "Esperando conexiones..." << endl;

  int serverSeqNum = 1000;
  int espacioDisponible = 777 - 12; // 765 bytes máximo

  while (true) {
    cout << "\n--- Esperando mensajes ---" << endl;

    n = recvfrom(sockfd, (char *)buffer, PROTOCOL_SIZE, MSG_WAITALL,
                 (struct sockaddr *)&cliaddr, (socklen_t *)&len);
    buffer[n] = '\0';

    char tipo = buffer[0];

    if (tipo == 'D') {
      // Mensaje DATA del cliente
      int seqNum = extractSeqNum(buffer);
      string datos = extraerDatos(buffer);

      cout << "Mensaje recibido - Seq: " << seqNum << endl;
      cout << "Cliente dice: " << datos << endl;

      int hashRecibido = buffer[11] - '0';
      int hashCalculado = makeHash(buffer, n);

      if (hashRecibido == hashCalculado) {
        enviarACK(sockfd, seqNum, cliaddr, len);
        cout << "Checksum CORRECTO - ACK enviado" << endl;
      } else {
        enviarNACK(sockfd, seqNum, cliaddr, len);
        cout << "Checksum INCORRECTO - NACK enviado" << endl;
      }
    } else if (tipo == 'A' || tipo == 'N') {

      int seqNum = extractSeqNum(buffer);
      cout << (tipo == 'A' ? "ACK" : "NACK") << " recibido para seq: " << seqNum
           << endl;
      continue;
    }

    // Recibir más fragmentos (no bloqueante)
    bool moreFragments = true;
    while (moreFragments) {
      n = recvfrom(sockfd, (char *)buffer, PROTOCOL_SIZE, MSG_DONTWAIT,
                   (struct sockaddr *)&cliaddr, (socklen_t *)&len);

      if (n > 0) {
        buffer[n] = '\0';
        tipo = buffer[0];

        if (tipo == 'D') {
          int currentSeqNum = extractSeqNum(buffer);
          string datos = extraerDatos(buffer);

          cout << "Fragmento adicional - Seq: " << currentSeqNum << endl;
          cout << "Cliente dice: " << datos << endl;

          int hashRecibido = buffer[11] - '0';
          int hashCalculado = makeHash(buffer, n);

          if (hashRecibido == hashCalculado) {
            enviarACK(sockfd, currentSeqNum, cliaddr, len);
            cout << "Checksum CORRECTO - ACK enviado" << endl;
          } else {
            enviarNACK(sockfd, currentSeqNum, cliaddr, len);
            cout << "Checksum INCORRECTO - NACK enviado" << endl;
          }
        }
      } else {
        moreFragments = false;
      }
    }

    // Pedir respuesta al usuario
    cout << "\nEscribe respuesta (q para salir): ";
    string input;
    getline(cin, input);

    if (input == "q" || input == "Q") {
      break;
    }

    // fragmentacion
    int longitudMensaje = input.size();
    int totalFragmentos =
        (longitudMensaje + espacioDisponible - 1) / espacioDisponible;

    if (totalFragmentos > 1) {

      for (int fragmento = 0; fragmento < totalFragmentos; fragmento++) {
        char respuestaDatagram[PROTOCOL_SIZE + 1];
        memset(respuestaDatagram, 0, PROTOCOL_SIZE + 1);

        respuestaDatagram[0] = 'D';

        // Seq number del servidor
        serverSeqNum++;
        char seqBuf[11];
        sprintf(seqBuf, "%010d", serverSeqNum);
        strncpy(respuestaDatagram + 1, seqBuf, 10);

        // Datos del fragmento
        int inicio = fragmento * espacioDisponible;
        int fin = min((fragmento + 1) * espacioDisponible, longitudMensaje);
        int bytesEsteFragmento = fin - inicio;
        strncpy(respuestaDatagram + 12, input.c_str() + inicio,
                bytesEsteFragmento);

        // Padding
        for (int p = 12 + bytesEsteFragmento; p < 777; p++) {
          respuestaDatagram[p] = '#';
        }
        respuestaDatagram[777] = '\0';

        // Checksum
        respuestaDatagram[11] = '0';
        int checkSum = makeHash(respuestaDatagram, PROTOCOL_SIZE);
        respuestaDatagram[11] = '0' + checkSum;

        servidorEnviarYConfirmar(sockfd, respuestaDatagram, serverSeqNum,
                                 cliaddr, len);
      }

    } else {
      // Mensaje NO fragmentado
      char respuestaDatagram[PROTOCOL_SIZE + 1];
      memset(respuestaDatagram, 0, PROTOCOL_SIZE + 1);

      respuestaDatagram[0] = 'D';

      serverSeqNum++;
      char seqBuf[11];
      sprintf(seqBuf, "%010d", serverSeqNum);
      strncpy(respuestaDatagram + 1, seqBuf, 10);

      int bytesACopiar = min((int)input.size(), 765);
      strncpy(respuestaDatagram + 12, input.c_str(), bytesACopiar);

      for (int p = 12 + bytesACopiar; p < 777; p++) {
        respuestaDatagram[p] = '#';
      }
      respuestaDatagram[777] = '\0';

      int checkSum = makeHash(respuestaDatagram, PROTOCOL_SIZE);
      respuestaDatagram[11] = '0' + checkSum;

      servidorEnviarYConfirmar(sockfd, respuestaDatagram, serverSeqNum, cliaddr,
                               len);
    }
  }

  close(sockfd);
  return 0;
}