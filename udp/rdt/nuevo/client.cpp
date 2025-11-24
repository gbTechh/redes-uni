// Client side implementation of UDP client-server model in C++
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// Socket libraries
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

const int PORT = 9999;
const int PROTOCOL_SIZE = 777;
const int BUFFER_SIZE = 777 + 1;

int makeHash(char *datagram, int size) {
  int sum = 0;
  for (int i = 0; i < size; i++) {
    if (i != 11) {
      sum += static_cast<int>(datagram[i]);
    }
  }
  return sum % 6;
}

int extractSeqNum(char *datagram) {
  char seqStr[11];
  strncpy(seqStr, datagram + 1, 10);
  seqStr[10] = '\0';
  return stoi(seqStr);
}

bool enviarYConfirmar(int sockfd, char *datagram, int seqNum,
                      struct sockaddr_in servaddr, int addrlen) {
  int intentos = 0;
  const int MAX_INTENTOS = 3;
  char buffer[BUFFER_SIZE];
  int len = sizeof(servaddr);

  while (intentos < MAX_INTENTOS) {
    // Enviar fragmento
    sendto(sockfd, datagram, PROTOCOL_SIZE, MSG_CONFIRM,
           (struct sockaddr *)&servaddr, addrlen);

    cout << "Enviado fragmento seq " << seqNum << " (intento " << (intentos + 1)
         << ")" << endl;

    int n = recvfrom(sockfd, buffer, PROTOCOL_SIZE, MSG_DONTWAIT,
                     (struct sockaddr *)&servaddr, (socklen_t *)&len);

    if (n > 0) {
      buffer[n] = '\0';
      char tipo = buffer[0];
      int seqRecibido = extractSeqNum(buffer);

      if (tipo == 'A' && seqRecibido == seqNum) {
        cout << "ACK recibido para seq " << seqNum << endl;
        return true;

      } else if (tipo == 'N' && seqRecibido == seqNum) {
        cout << "NACK recibido para seq " << seqNum << ", reintento "
             << (intentos + 1) << endl;
        break;
      } else if (tipo == 'D') {
        string datos = string(buffer + 12);
        size_t paddingPos = datos.find('#');
        if (paddingPos != string::npos) {
          datos = datos.substr(0, paddingPos);
        }
        cout << "\nServidor dice: " << datos << endl;
      }
    }
    usleep(10000); // 10ms

    intentos++;
  }

  return false;
}

int main() {
  int sockfd;
  char buffer[BUFFER_SIZE];
  struct hostent *host;
  string hello = "Hello from client";
  struct sockaddr_in servaddr;

  host = gethostbyname("127.0.0.1");

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  memset(&servaddr, 0, sizeof(servaddr));

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(PORT);
  servaddr.sin_addr = *((struct in_addr *)host->h_addr);

  int n, len;
  char buf[11]; // 10 + \0
  int seqNum = 0;
  char datagram[PROTOCOL_SIZE + 1]; // 777 + 1 for null terminator
  int checkSum = 0;

  int espacioDisponible = 777 - 12; // 765 bytes máximo

  cout << "Cliente UDP RDT iniciado" << endl;

  while (true) {
    cout << "\nType Something (q or Q to quit): ";

    string input;
    getline(cin, input);

    // strncpy(buffer, input.c_str(), BUFFER_SIZE - 1);
    // buffer[BUFFER_SIZE - 1] = '\0';

    if (input == "q" || input == "Q") {
      break;
    }

    // FRAGMENTACION
    int longitudMensaje = input.size();
    int totalFragmentos =
        (longitudMensaje + espacioDisponible - 1) / espacioDisponible;
    if (totalFragmentos > 1) {
      cout << "Fragmentando mensaje en " << totalFragmentos << " partes\n";

      for (int fragmento = 0; fragmento < totalFragmentos; fragmento++) {
        memset(datagram, 0, PROTOCOL_SIZE + 1);

        datagram[0] = 'D';

        seqNum++;
        sprintf(buf, "%010d", seqNum);
        strncpy(datagram + 1, buf, 10);

        int inicio = fragmento * espacioDisponible;
        int fin = min((fragmento + 1) * espacioDisponible, longitudMensaje);
        int bytesEsteFragmento = fin - inicio;

        strncpy(datagram + 12, input.c_str() + inicio, bytesEsteFragmento);

        for (int p = 12 + bytesEsteFragmento; p < 777; p++) {
          datagram[p] = '#';
        }
        datagram[777] = '\0';
        datagram[11] = '0';
        checkSum = makeHash(datagram, 777);
        datagram[11] = '0' + checkSum;
        cout << ">>" << datagram << "<<" << endl;

        enviarYConfirmar(sockfd, datagram, seqNum, servaddr, sizeof(servaddr));
        cout << "Fragmento " << (fragmento + 1) << "/" << totalFragmentos
             << " enviado (seq=" << seqNum << ")" << endl;
      }

    } else {

      memset(datagram, 0, PROTOCOL_SIZE + 1);
      datagram[0] = 'D';

      seqNum++;
      sprintf(buf, "%010d", seqNum);
      strncpy(datagram + 1, buf, 10);

      int bytesACopiar = min(longitudMensaje, espacioDisponible);
      strncpy(datagram + 12, input.c_str(), bytesACopiar);
      cout << ">>" << datagram << "<<" << endl;

      for (int p = 12 + bytesACopiar; p < 777; p++) {
        datagram[p] = '#';
      }
      datagram[777] = '\0';
      datagram[11] = '0';
      checkSum = makeHash(datagram, 777);
      datagram[11] = '0' + checkSum;

      cout << ">>" << datagram << "<<" << endl;

      enviarYConfirmar(sockfd, datagram, seqNum, servaddr, sizeof(servaddr));
    }

    cout << "Escuchando mensajes del servidor..." << endl;

    for (int i = 0; i < 100; i++) {
      n = recvfrom(sockfd, buffer, PROTOCOL_SIZE, MSG_DONTWAIT,
                   (struct sockaddr *)&servaddr, (socklen_t *)&len);

      if (n > 0 && buffer[0] == 'D') {
        string datos = string(buffer + 12);
        size_t paddingPos = datos.find('#');
        if (paddingPos != string::npos) {
          datos = datos.substr(0, paddingPos);
        }
        cout << "\nServidor dice: " << datos << endl;

        int serverSeq = extractSeqNum(buffer);
        int hashRecibido = buffer[11] - '0';
        int hashCalculado = makeHash(buffer, n);

        if (hashRecibido == hashCalculado) {

          char ackDatagram[PROTOCOL_SIZE + 1];
          memset(ackDatagram, 0, PROTOCOL_SIZE + 1);
          ackDatagram[0] = 'A';
          sprintf(buf, "%010d", serverSeq);
          strncpy(ackDatagram + 1, buf, 10);
          for (int p = 12; p < 777; p++)
            ackDatagram[p] = '#';
          ackDatagram[777] = '\0';
          ackDatagram[11] = '0';
          int ackChecksum = makeHash(ackDatagram, PROTOCOL_SIZE);
          ackDatagram[11] = '0' + ackChecksum;

          sendto(sockfd, ackDatagram, PROTOCOL_SIZE, MSG_CONFIRM,
                 (struct sockaddr *)&servaddr, sizeof(servaddr));
          cout << "✅ ACK enviado al servidor" << endl;
        }
      } else {
        usleep(10000); // 10ms
      }
    }
  }

  close(sockfd);
  return 0;
}