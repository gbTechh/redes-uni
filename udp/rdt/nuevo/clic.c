// Client side implementation of UDP client-server model in C
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 9999
#define PROTOCOL_SIZE 777
#define BUFFER_SIZE 778 // 777 + 1

int makeHash(char *datagram, int size) {
  int sum = 0;
  for (int i = 0; i < size; i++) {
    if (i != 11) {
      sum += (unsigned char)datagram[i];
    }
  }
  return sum % 6;
}

int extractSeqNum(char *datagram) {
  char seqStr[11];
  strncpy(seqStr, datagram + 1, 10);
  seqStr[10] = '\0';
  return atoi(seqStr);
}

int enviarYConfirmar(int sockfd, char *datagram, int seqNum,
                     struct sockaddr_in servaddr, int addrlen) {
  int intentos = 0;
  const int MAX_INTENTOS = 3;
  char buffer[BUFFER_SIZE];
  int len = sizeof(servaddr);

  while (intentos < MAX_INTENTOS) {
    // Enviar fragmento
    sendto(sockfd, datagram, PROTOCOL_SIZE, MSG_CONFIRM,
           (struct sockaddr *)&servaddr, addrlen);

    printf("Enviado fragmento seq %d (intento %d)\n", seqNum, intentos + 1);

    int n = recvfrom(sockfd, buffer, PROTOCOL_SIZE, MSG_DONTWAIT,
                     (struct sockaddr *)&servaddr, (socklen_t *)&len);

    if (n > 0) {
      buffer[n] = '\0';
      char tipo = buffer[0];
      int seqRecibido = extractSeqNum(buffer);

      if (tipo == 'A' && seqRecibido == seqNum) {
        printf("ACK recibido para seq %d\n", seqNum);
        return 1;

      } else if (tipo == 'N' && seqRecibido == seqNum) {
        printf("NACK recibido para seq %d, reintento %d\n", seqNum,
               intentos + 1);
        break;
      } else if (tipo == 'D') {
        // Extraer datos sin padding
        char datos[766] = {0};
        strncpy(datos, buffer + 12, 765);
        char *padding_pos = strchr(datos, '#');
        if (padding_pos != NULL) {
          *padding_pos = '\0';
        }
        printf("\nServidor dice: %s\n", datos);
      }
    }
    usleep(10000); // 10ms
    intentos++;
  }
  return 0;
}

int main() {
  int sockfd;
  char buffer[BUFFER_SIZE];
  struct hostent *host;
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
  char datagram[PROTOCOL_SIZE + 1]; // 777 + 1
  int checkSum = 0;

  int espacioDisponible = 777 - 12; // 765 bytes máximo

  printf("Cliente UDP RDT iniciado\n");

  while (1) {
    printf("\nType Something (q or Q to quit): ");

    char input[1024];
    if (fgets(input, sizeof(input), stdin) == NULL) {
      break;
    }

    // Remover newline
    input[strcspn(input, "\n")] = 0;

    if (strcmp(input, "q") == 0 || strcmp(input, "Q") == 0) {
      break;
    }

    // FRAGMENTACION
    int longitudMensaje = strlen(input);
    int totalFragmentos =
        (longitudMensaje + espacioDisponible - 1) / espacioDisponible;

    if (totalFragmentos > 1) {
      printf("Fragmentando mensaje en %d partes\n", totalFragmentos);

      for (int fragmento = 0; fragmento < totalFragmentos; fragmento++) {
        memset(datagram, 0, PROTOCOL_SIZE + 1);

        datagram[0] = 'D';

        seqNum++;
        snprintf(buf, sizeof(buf), "%010d", seqNum);
        strncpy(datagram + 1, buf, 10);

        int inicio = fragmento * espacioDisponible;
        int fin = (fragmento + 1) * espacioDisponible;
        if (fin > longitudMensaje) {
          fin = longitudMensaje;
        }
        int bytesEsteFragmento = fin - inicio;

        strncpy(datagram + 12, input + inicio, bytesEsteFragmento);

        for (int p = 12 + bytesEsteFragmento; p < 777; p++) {
          datagram[p] = '#';
        }
        datagram[777] = '\0';
        datagram[11] = '0';
        checkSum = makeHash(datagram, PROTOCOL_SIZE);
        datagram[11] = '0' + checkSum;

        printf(">>%s<<\n", datagram);

        enviarYConfirmar(sockfd, datagram, seqNum, servaddr, sizeof(servaddr));
        printf("Fragmento %d/%d enviado (seq=%d)\n", fragmento + 1,
               totalFragmentos, seqNum);
      }

    } else {
      memset(datagram, 0, PROTOCOL_SIZE + 1);
      datagram[0] = 'D';

      seqNum++;
      snprintf(buf, sizeof(buf), "%010d", seqNum);
      strncpy(datagram + 1, buf, 10);

      int bytesACopiar = longitudMensaje;
      if (bytesACopiar > espacioDisponible) {
        bytesACopiar = espacioDisponible;
      }
      strncpy(datagram + 12, input, bytesACopiar);
      printf(">>%s<<\n", datagram);

      for (int p = 12 + bytesACopiar; p < 777; p++) {
        datagram[p] = '#';
      }
      datagram[777] = '\0';
      datagram[11] = '0';
      checkSum = makeHash(datagram, PROTOCOL_SIZE);
      datagram[11] = '0' + checkSum;

      printf(">>%s<<\n", datagram);

      enviarYConfirmar(sockfd, datagram, seqNum, servaddr, sizeof(servaddr));
    }

    printf("Escuchando mensajes del servidor...\n");

    for (int i = 0; i < 100; i++) {
      n = recvfrom(sockfd, buffer, PROTOCOL_SIZE, MSG_DONTWAIT,
                   (struct sockaddr *)&servaddr, (socklen_t *)&len);

      if (n > 0 && buffer[0] == 'D') {
        char datos[766] = {0};
        strncpy(datos, buffer + 12, 765);
        char *padding_pos = strchr(datos, '#');
        if (padding_pos != NULL) {
          *padding_pos = '\0';
        }
        printf("\nServidor dice: %s\n", datos);

        int serverSeq = extractSeqNum(buffer);
        int hashRecibido = buffer[11] - '0';
        int hashCalculado = makeHash(buffer, n);

        if (hashRecibido == hashCalculado) {
          char ackDatagram[PROTOCOL_SIZE + 1];
          memset(ackDatagram, 0, PROTOCOL_SIZE + 1);
          ackDatagram[0] = 'A';
          snprintf(buf, sizeof(buf), "%010d", serverSeq);
          strncpy(ackDatagram + 1, buf, 10);
          for (int p = 12; p < 777; p++) {
            ackDatagram[p] = '#';
          }
          ackDatagram[777] = '\0';
          ackDatagram[11] = '0';
          int ackChecksum = makeHash(ackDatagram, PROTOCOL_SIZE);
          ackDatagram[11] = '0' + ackChecksum;

          sendto(sockfd, ackDatagram, PROTOCOL_SIZE, MSG_CONFIRM,
                 (struct sockaddr *)&servaddr, sizeof(servaddr));
          printf("✅ ACK enviado al servidor\n");
        }
      } else {
        usleep(10000); // 10ms
      }
    }
  }

  close(sockfd);
  return 0;
}