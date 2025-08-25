/* Client code in C */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 255
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 45000

int main(void) {
  struct sockaddr_in stSockAddr;
  int Res;
  int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  char *buffer = malloc(BUFFER_SIZE);
  if (buffer == NULL) {
    perror("Error allocating memory");
    close(SocketFD);
    exit(EXIT_FAILURE);
  }

  if (-1 == SocketFD) {
    perror("cannot create socket");
    exit(EXIT_FAILURE);
  }

  memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

  stSockAddr.sin_family = AF_INET;
  stSockAddr.sin_port = htons(SERVER_PORT);
  Res = inet_pton(AF_INET, SERVER_IP, &stSockAddr.sin_addr);

  if (0 > Res) {
    perror("error: first parameter is not a valid address family");
    close(SocketFD);
    exit(EXIT_FAILURE);
  } else if (0 == Res) {
    perror("char string (second parameter does not contain valid ipaddress");
    close(SocketFD);
    exit(EXIT_FAILURE);
  }

  if (-1 == connect(SocketFD, (const struct sockaddr *)&stSockAddr,
                    sizeof(struct sockaddr_in))) {
    perror("connect failed");
    close(SocketFD);
    exit(EXIT_FAILURE);
  }

  printf("Conectado al servidor %s:%d\n", SERVER_IP, SERVER_PORT);
  printf("Escribe mensajes (escribe 'chau' para salir):\n");

  while (1) {
    printf("Tu: ");
    fflush(stdout);

    if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
      break;
    }

    // Remover salto de línea
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
      len--;
    }

    // Enviar mensaje al servidor
    ssize_t bytes_sent = write(SocketFD, buffer, len);
    if (bytes_sent <= 0) {
      perror("write failed");
      break;
    }

    if (strcmp(buffer, "chau") == 0) {
      printf("Saliendo...\n");
      ssize_t bytes_received = read(SocketFD, buffer, BUFFER_SIZE - 1);
      if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Servidor: %s\n", buffer);
      }
      break;
    }

    // Leer respuesta del servidor
    ssize_t bytes_received = read(SocketFD, buffer, BUFFER_SIZE - 1);

    if (bytes_received <= 0) {
      if (bytes_received == 0) {
        printf("Servidor cerró la conexión\n");
      } else {
        perror("read failed");
      }
      break;
    }

    buffer[bytes_received] = '\0';
    printf("Servidor: %s\n", buffer);
  }

  free(buffer);
  shutdown(SocketFD, SHUT_RDWR);
  close(SocketFD);
  return 0;
}