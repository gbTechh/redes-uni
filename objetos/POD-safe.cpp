#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
$ g++ POD-safe.cpp -o POD-safe.exe
$ ./POD-safe.exe server 8080
Received id=42, value=3.14159
-----------------------------
$./POD-safe.exe client 127.0.0.1 8080
*/
// Plain old data (safe to cast)
struct PlainData {
    int id;
    double value;
};

void run_server(uint16_t port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    bind(server_fd, (sockaddr *)&address, sizeof(address));
    listen(server_fd, 1);

    int client_fd = accept(server_fd, nullptr, nullptr);
    PlainData recv_data{};
    recv(client_fd, &recv_data, sizeof(recv_data), 0);

    std::cout << "Received id=" << recv_data.id
              << ", value=" << recv_data.value << std::endl;

    close(client_fd);
    close(server_fd);
}

void run_client(const char *ip, uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serv_addr.sin_addr);

    connect(sock, (sockaddr *)&serv_addr, sizeof(serv_addr));

    PlainData d{42, 3.14159};
    send(sock, &d, sizeof(d), 0);

    close(sock);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:\n  " << argv[0] << " server <port>\n  "
                  << argv[0] << " client <ip> <port>\n";
        return 1;
    }

    std::string mode = argv[1];
    if (mode == "server" && argc == 3) {
        run_server(std::stoi(argv[2]));
    } else if (mode == "client" && argc == 4) {
        run_client(argv[2], std::stoi(argv[3]));
    }
}

