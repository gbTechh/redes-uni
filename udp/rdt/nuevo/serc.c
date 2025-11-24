// Server side implementation of UDP client-server model in C
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 9999
#define PROTOCOL_SIZE 777
#define BUFFER_SIZE 778 // 777 + 1

int extractSeqNum(char *datagram) {
  char seqStr[11];
  strncpy(seqStr, datagram + 1, 10);
  seqStr[10] = '\0';
  return atoi(seqStr);
}

int makeHash(char *datagram, int size) {
  int sum = 0;
  for (int i = 0; i < size; i++) {
    if (i != 11) {
      sum += (unsigned char)datagram[i];
    }
  }
  return sum % 6;
}

void enviarACK(int sockfd, int seqNum, struct sockaddr_in cliaddr, int len) {
  char ackDatagram[PROTOCOL_SIZE + 1];
  memset(ackDatagram, 0, PROTOCOL_SIZE + 1);

  ackDatagram[0] = 'A';

  char buf[11];
  snprintf(buf, sizeof(buf), "%010d", seqNum);
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
  snprintf(buf, sizeof(buf), "%010d", seqNum);
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

void extraerDatos(char *datagram, char *datos) {
  strncpy(datos, datagram + 12, 765);
  datos[765] = '\0';
  char *padding_pos = strchr(datos, '#');
  if (padding_pos != NULL) {
    *padding_pos = '\0';
  }
}

int servidorEnviarYConfirmar(int sockfd, char *datagram, int seqNum,
                             struct sockaddr_in cliaddr, int addrlen) {
  int intentos = 0;
  const int MAX_INTENTOS = 3;
  char buffer[BUFFER_SIZE];
  int len = sizeof(cliaddr);

  while (intentos < MAX_INTENTOS) {
    sendto(sockfd, datagram, PROTOCOL_SIZE, MSG_CONFIRM,
           (struct sockaddr *)&cliaddr, addrlen);

    printf("Servidor envió seq %d (intento %d)\n", seqNum, intentos + 1);

    int n = recvfrom(sockfd, buffer, PROTOCOL_SIZE, MSG_DONTWAIT,
                     (struct sockaddr *)&cliaddr, (socklen_t *)&len);

    if (n > 0) {
      buffer[n] = '\0';
      char tipo = buffer[0];
      int seqRecibido = extractSeqNum(buffer);

      if (tipo == 'A' && seqRecibido == seqNum) {
        printf("ACK recibido del cliente para seq %d\n", seqNum);
        return 1;
      } else if (tipo == 'N' && seqRecibido == seqNum) {
        printf("NACK del cliente para seq %d, reintento %d\n", seqNum,
               intentos + 1);
        break;
      } else if (tipo == 'D') {
        // Mensaje nuevo del cliente
        char datos[766];
        extraerDatos(buffer, datos);
        printf("\nCliente dice: %s\n", datos);
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
  return 0;
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

  printf("Servidor UDP RDT iniciado en puerto %d\n", PORT);
  printf("Esperando conexiones...\n");

  int serverSeqNum = 1000;
  int espacioDisponible = 777 - 12; // 765 bytes máximo

  while (1) {
    printf("\n--- Esperando mensajes ---\n");

    n = recvfrom(sockfd, (char *)buffer, PROTOCOL_SIZE, MSG_WAITALL,
                 (struct sockaddr *)&cliaddr, (socklen_t *)&len);
    buffer[n] = '\0';

    char tipo = buffer[0];

    if (tipo == 'D') {
      // Mensaje DATA del cliente
      int seqNum = extractSeqNum(buffer);
      char datos[766];
      extraerDatos(buffer, datos);

      printf("Mensaje recibido - Seq: %d\n", seqNum);
      printf("Cliente dice: %s\n", datos);

      int hashRecibido = buffer[11] - '0';
      int hashCalculado = makeHash(buffer, n);

      if (hashRecibido == hashCalculado) {
        enviarACK(sockfd, seqNum, cliaddr, len);
        printf("Checksum CORRECTO - ACK enviado\n");
      } else {
        enviarNACK(sockfd, seqNum, cliaddr, len);
        printf("Checksum INCORRECTO - NACK enviado\n");
      }
    } else if (tipo == 'A' || tipo == 'N') {
      int seqNum = extractSeqNum(buffer);
      printf("%s recibido para seq: %d\n", (tipo == 'A' ? "ACK" : "NACK"),
             seqNum);
      continue;
    }

    // Recibir más fragmentos (no bloqueante)
    int moreFragments = 1;
    while (moreFragments) {
      n = recvfrom(sockfd, (char *)buffer, PROTOCOL_SIZE, MSG_DONTWAIT,
                   (struct sockaddr *)&cliaddr, (socklen_t *)&len);

      if (n > 0) {
        buffer[n] = '\0';
        tipo = buffer[0];

        if (tipo == 'D') {
          int currentSeqNum = extractSeqNum(buffer);
          char datos[766];
          extraerDatos(buffer, datos);

          printf("Fragmento adicional - Seq: %d\n", currentSeqNum);
          printf("Cliente dice: %s\n", datos);

          int hashRecibido = buffer[11] - '0';
          int hashCalculado = makeHash(buffer, n);

          if (hashRecibido == hashCalculado) {
            enviarACK(sockfd, currentSeqNum, cliaddr, len);
            printf("Checksum CORRECTO - ACK enviado\n");
          } else {
            enviarNACK(sockfd, currentSeqNum, cliaddr, len);
            printf("Checksum INCORRECTO - NACK enviado\n");
          }
        }
      } else {
        moreFragments = 0;
      }
    }

    // Pedir respuesta al usuario
    printf("\nEscribe respuesta (q para salir): ");
    char input[1024];
    if (fgets(input, sizeof(input), stdin) == NULL) {
      break;
    }

    // Remover newline
    input[strcspn(input, "\n")] = 0;

    if (strcmp(input, "q") == 0 || strcmp(input, "Q") == 0) {
      break;
    }

    // fragmentacion
    int longitudMensaje = strlen(input);
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
        snprintf(seqBuf, sizeof(seqBuf), "%010d", serverSeqNum);
        strncpy(respuestaDatagram + 1, seqBuf, 10);

        // Datos del fragmento
        int inicio = fragmento * espacioDisponible;
        int fin = (fragmento + 1) * espacioDisponible;
        if (fin > longitudMensaje) {
          fin = longitudMensaje;
        }
        int bytesEsteFragmento = fin - inicio;
        strncpy(respuestaDatagram + 12, input + inicio, bytesEsteFragmento);

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
      snprintf(seqBuf, sizeof(seqBuf), "%010d", serverSeqNum);
      strncpy(respuestaDatagram + 1, seqBuf, 10);

      int bytesACopiar = longitudMensaje;
      if (bytesACopiar > 765) {
        bytesACopiar = 765;
      }
      strncpy(respuestaDatagram + 12, input, bytesACopiar);

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