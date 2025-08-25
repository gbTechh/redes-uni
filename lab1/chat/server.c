/* Server code in C */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 255
#define EXIT_WORD "chau"

int main(void) {
  struct sockaddr_in stSockAddr;
  int SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  int n;

  if (-1 == SocketFD) {
    perror("can not create socket");
    exit(EXIT_FAILURE);
  }

  // problema de addr en uso
  int opt = 1;
  if (setsockopt(SocketFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt failed");
    close(SocketFD);
    exit(EXIT_FAILURE);
  }

  memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

  stSockAddr.sin_family = AF_INET;
  stSockAddr.sin_port = htons(45000);
  stSockAddr.sin_addr.s_addr = INADDR_ANY;

  if (-1 == bind(SocketFD, (const struct sockaddr *)&stSockAddr,
                 sizeof(struct sockaddr_in))) {
    perror("error bind failed");
    close(SocketFD);
    exit(EXIT_FAILURE);
  }

  if (-1 == listen(SocketFD, 10)) {
    perror("error listen failed");
    close(SocketFD);
    exit(EXIT_FAILURE);
  }

  for (;;) {
    printf("Esperando cliente...\n");
    int ConnectFD = accept(SocketFD, NULL, NULL);

    if (0 > ConnectFD) {
      perror("error accept failed");
      continue;
    }

    printf("Cliente %d conectado!\n", ConnectFD);
    printf("¡Chat iniciado!..\n");

    while (1) {
      char *buffer = malloc(BUFFER_SIZE);
      if (buffer == NULL) {
        perror("Error allocating memory");
        close(ConnectFD);
        break;
      }

      ssize_t n = read(ConnectFD, buffer, BUFFER_SIZE - 1);

      if (n <= 0) {
        printf("Cliente %d desconectado\n", ConnectFD);
        free(buffer);
        break;
      }

      buffer[n] = '\0';

      // Remover salto de línea si existe
      if (n > 0 && buffer[n - 1] == '\n') {
        buffer[n - 1] = '\0';
        n--;
      }

      printf("Cliente %d: %s\n", ConnectFD, buffer);

      if (strcmp(buffer, EXIT_WORD) == 0) {
        printf("Cliente %d solicitó salir\n", ConnectFD);
        // Enviar confirmación antes de cerrar
        char *goodbye = "Adiós!";
        write(ConnectFD, goodbye, strlen(goodbye));
        free(buffer);
        break;
      }

      // server msg
      char *response = malloc(BUFFER_SIZE);
      if (response == NULL) {
        perror("Error allocating memory for response");
        free(buffer);
        break;
      }

      printf("Servidor: ");
      fflush(stdout);

      if (fgets(response, BUFFER_SIZE, stdin) == NULL) {
        free(buffer);
        free(response);
        break;
      }

      // Remover salto de línea de la respuesta
      size_t len = strlen(response);
      if (len > 0 && response[len - 1] == '\n') {
        response[len - 1] = '\0';
        len--;
      }

      // Verificar si el servidor quiere terminar
      if (strcmp(response, EXIT_WORD) == 0) {
        printf("Servidor terminando conversación con cliente %d\n", ConnectFD);
        write(ConnectFD, response, len);
        free(buffer);
        free(response);
        break;
      }

      ssize_t bytes_sent = write(ConnectFD, response, len);
      if (bytes_sent <= 0) {
        perror("Error enviando respuesta");
        free(buffer);
        free(response);
        break;
      }

      free(buffer);
      free(response);
    }

    shutdown(ConnectFD, SHUT_RDWR);
    close(ConnectFD);
    printf("Conexión con cliente cerrada.\n\n");
  }

  close(SocketFD);
  return 0;
}