#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
/*
$ g++ cs.cpp -o cs.exe
$ ./cs.exe server 8080
Server listening on port 8080...
Derived{id=42, name=HelloWorld}
-------------------------------
$ ./cs.exe client 127.0.0.1 8080
Sent Derived object
*/
// Base class
class Base {
public:
    virtual ~Base() = default;
    virtual void print() const = 0;
};

// Derived class
class Derived : public Base {
public:
    int id;
    std::string name;

    Derived(int i = 0, std::string n = "") : id(i), name(std::move(n)) {}

    void print() const override {
        std::cout << "Derived{id=" << id << ", name=" << name << "}" << std::endl;
    }

    // Serialize to bytes
    std::vector<char> to_bytes() const {
        uint32_t id_net = htonl(id);
        uint32_t len_net = htonl(name.size());

        std::vector<char> buffer(sizeof(id_net) + sizeof(len_net) + name.size());
        memcpy(buffer.data(), &id_net, sizeof(id_net));
        memcpy(buffer.data() + sizeof(id_net), &len_net, sizeof(len_net));
        memcpy(buffer.data() + sizeof(id_net) + sizeof(len_net), name.data(), name.size());
        return buffer;
    }

    // Deserialize from bytes
    static Derived from_bytes(const char *data, size_t size) {
        if (size < 8) throw std::runtime_error("Invalid buffer size");

        uint32_t id_net, len_net;
        memcpy(&id_net, data, 4);
        memcpy(&len_net, data + 4, 4);

        int id = ntohl(id_net);
        uint32_t len = ntohl(len_net);

        if (size < 8 + len) throw std::runtime_error("Invalid string length");

        std::string name(data + 8, len);
        return Derived(id, name);
    }
};

// Server
void run_server(uint16_t port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    bind(server_fd, (sockaddr *)&address, sizeof(address));
    listen(server_fd, 1);

    std::cout << "Server listening on port " << port << "...\n";

    int client_fd = accept(server_fd, nullptr, nullptr);
    char buffer[1024];
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer), 0);

    Derived d = Derived::from_bytes(buffer, bytes);
    Base *b = &d;  // Cast to base
    b->print();

    close(client_fd);
    close(server_fd);
}

// Client
void run_client(const char *ip, uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serv_addr.sin_addr);

    connect(sock, (sockaddr *)&serv_addr, sizeof(serv_addr));

    Derived d(42, "HelloWorld");
    auto buffer = d.to_bytes();

    send(sock, buffer.data(), buffer.size(), 0);
    std::cout << "Sent Derived object\n";

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
    } else {
        std::cerr << "Invalid arguments\n";
        return 1;
    }
    return 0;
}

