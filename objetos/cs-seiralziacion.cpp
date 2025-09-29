#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

/*
COMPILACIÓN Y USO:
==================
$ g++ -std=c++11 simple_serialization.cpp -o simple.exe

Terminal 1 (Servidor):
$ ./simple.exe server 8080

Terminal 2 (Cliente):
$ ./simple.exe client 127.0.0.1 8080

CONCEPTOS CLAVE:
================
1. Siempre usar htonl/ntohl para convertir números (endianness)
2. Para strings: enviar primero el tamaño, luego los caracteres
3. Para punteros/objetos: marcar si existe (true/false) antes de enviar
4. Para vectores: enviar primero cuántos elementos hay
*/

// ============================================================================
// CLASE SIMPLE: Punto 2D
// ============================================================================
class Point {
public:
  int x;
  int y;

  Point(int x = 0, int y = 0) : x(x), y(y) {}

  // Serializar (convertir a bytes)
  std::vector<char> to_bytes() const {
    std::vector<char> buffer;

    // Convertir x e y a network byte order (big-endian)
    uint32_t x_net = htonl(x);
    uint32_t y_net = htonl(y);

    // Agregar al buffer
    buffer.insert(buffer.end(), (char *)&x_net, (char *)&x_net + 4);
    buffer.insert(buffer.end(), (char *)&y_net, (char *)&y_net + 4);

    return buffer; // Total: 8 bytes
  }

  // Deserializar (crear desde bytes)
  static Point from_bytes(const char *data) {
    uint32_t x_net, y_net;

    // Copiar desde el buffer
    memcpy(&x_net, data, 4);
    memcpy(&y_net, data + 4, 4);

    // Convertir de network a host byte order
    int x = ntohl(x_net);
    int y = ntohl(y_net);

    return Point(x, y);
  }

  void print() const { std::cout << "Point(" << x << ", " << y << ")"; }
};

// ============================================================================
// CLASE CON STRING: Persona
// ============================================================================
class Person {
public:
  int id;
  std::string name;
  int age;

  Person(int id = 0, std::string name = "", int age = 0)
      : id(id), name(name), age(age) {}

  // Serializar
  std::vector<char> to_bytes() const {
    std::vector<char> buffer;

    // 1. Serializar ID
    uint32_t id_net = htonl(id);
    buffer.insert(buffer.end(), (char *)&id_net, (char *)&id_net + 4);

    // 2. Serializar NAME (primero el tamaño, luego el contenido)
    uint32_t name_len = htonl(name.size());
    buffer.insert(buffer.end(), (char *)&name_len, (char *)&name_len + 4);
    buffer.insert(buffer.end(), name.begin(), name.end());

    // 3. Serializar AGE
    uint32_t age_net = htonl(age);
    buffer.insert(buffer.end(), (char *)&age_net, (char *)&age_net + 4);

    return buffer;
  }

  // Deserializar
  static Person from_bytes(const char *data, size_t size) {
    size_t pos = 0;

    // 1. Leer ID
    if (pos + 4 > size)
      throw std::runtime_error("Buffer too small");
    uint32_t id_net;
    memcpy(&id_net, data + pos, 4);
    int id = ntohl(id_net);
    pos += 4;

    // 2. Leer NAME
    if (pos + 4 > size)
      throw std::runtime_error("Buffer too small");
    uint32_t name_len_net;
    memcpy(&name_len_net, data + pos, 4);
    uint32_t name_len = ntohl(name_len_net);
    pos += 4;

    if (pos + name_len > size)
      throw std::runtime_error("Invalid name length");
    std::string name(data + pos, name_len);
    pos += name_len;

    // 3. Leer AGE
    if (pos + 4 > size)
      throw std::runtime_error("Buffer too small");
    uint32_t age_net;
    memcpy(&age_net, data + pos, 4);
    int age = ntohl(age_net);

    return Person(id, name, age);
  }

  void print() const {
    std::cout << "Person{id=" << id << ", name=\"" << name << "\", age=" << age
              << "}";
  }
};

// ============================================================================
// CLASE CON OBJETO ANIDADO: Empleado (tiene una Person y un Point)
// ============================================================================
class Employee {
public:
  Person info;            // Objeto anidado
  Point *office_location; // Puntero (puede ser nullptr)
  int salary;

  Employee(Person info = Person(), int salary = 0)
      : info(info), office_location(nullptr), salary(salary) {}

  ~Employee() { delete office_location; }

  // Serializar
  std::vector<char> to_bytes() const {
    std::vector<char> buffer;

    // 1. Serializar Person (objeto anidado)
    std::vector<char> person_bytes = info.to_bytes();
    buffer.insert(buffer.end(), person_bytes.begin(), person_bytes.end());

    // 2. Serializar puntero a Point
    // Primero indicamos si existe (1) o es nullptr (0)
    uint32_t has_location = (office_location != nullptr) ? 1 : 0;
    uint32_t has_location_net = htonl(has_location);
    buffer.insert(buffer.end(), (char *)&has_location_net,
                  (char *)&has_location_net + 4);

    // Si existe, serializamos el Point
    if (office_location != nullptr) {
      std::vector<char> point_bytes = office_location->to_bytes();
      buffer.insert(buffer.end(), point_bytes.begin(), point_bytes.end());
    }

    // 3. Serializar salary
    uint32_t salary_net = htonl(salary);
    buffer.insert(buffer.end(), (char *)&salary_net, (char *)&salary_net + 4);

    return buffer;
  }

  // Deserializar
  static Employee from_bytes(const char *data, size_t size) {
    size_t pos = 0;

    // 1. Deserializar Person
    // Necesitamos calcular cuántos bytes ocupa la Person
    // Formato: id(4) + name_len(4) + name(variable) + age(4)
    if (pos + 8 > size)
      throw std::runtime_error("Buffer too small");

    // Leer el tamaño del nombre para saber cuántos bytes leer
    uint32_t name_len_net;
    memcpy(&name_len_net, data + 4, 4); // El name_len está después del id
    uint32_t name_len = ntohl(name_len_net);

    size_t person_size = 4 + 4 + name_len + 4; // id + len + name + age
    if (pos + person_size > size)
      throw std::runtime_error("Buffer too small");

    Person info = Person::from_bytes(data + pos, person_size);
    pos += person_size;

    // 2. Deserializar puntero a Point
    if (pos + 4 > size)
      throw std::runtime_error("Buffer too small");
    uint32_t has_location_net;
    memcpy(&has_location_net, data + pos, 4);
    uint32_t has_location = ntohl(has_location_net);
    pos += 4;

    Point *office_location = nullptr;
    if (has_location == 1) {
      if (pos + 8 > size)
        throw std::runtime_error("Buffer too small");
      office_location = new Point(Point::from_bytes(data + pos));
      pos += 8; // Point ocupa 8 bytes
    }

    // 3. Deserializar salary
    if (pos + 4 > size)
      throw std::runtime_error("Buffer too small");
    uint32_t salary_net;
    memcpy(&salary_net, data + pos, 4);
    int salary = ntohl(salary_net);

    Employee emp(info, salary);
    emp.office_location = office_location;
    return emp;
  }

  void print() const {
    std::cout << "Employee{\n";
    std::cout << "  info: ";
    info.print();
    std::cout << ",\n  office: ";
    if (office_location) {
      office_location->print();
    } else {
      std::cout << "nullptr";
    }
    std::cout << ",\n  salary: $" << salary << "\n}";
  }
};

// ============================================================================
// CLASE CON VECTOR: Departamento (tiene múltiples empleados)
// ============================================================================
class Department {
public:
  std::string dept_name;
  std::vector<Employee> employees;

  Department(std::string name = "") : dept_name(name) {}

  // Serializar
  std::vector<char> to_bytes() const {
    std::vector<char> buffer;

    // 1. Serializar nombre del departamento
    uint32_t name_len = htonl(dept_name.size());
    buffer.insert(buffer.end(), (char *)&name_len, (char *)&name_len + 4);
    buffer.insert(buffer.end(), dept_name.begin(), dept_name.end());

    // 2. Serializar vector de empleados
    // Primero el número de empleados
    uint32_t num_employees = htonl(employees.size());
    buffer.insert(buffer.end(), (char *)&num_employees,
                  (char *)&num_employees + 4);

    // Luego cada empleado
    for (const Employee &emp : employees) {
      std::vector<char> emp_bytes = emp.to_bytes();

      // Guardamos el tamaño de cada empleado primero
      uint32_t emp_size = htonl(emp_bytes.size());
      buffer.insert(buffer.end(), (char *)&emp_size, (char *)&emp_size + 4);

      // Luego los datos del empleado
      buffer.insert(buffer.end(), emp_bytes.begin(), emp_bytes.end());
    }

    return buffer;
  }

  // Deserializar
  static Department from_bytes(const char *data, size_t size) {
    size_t pos = 0;

    // 1. Leer nombre del departamento
    if (pos + 4 > size)
      throw std::runtime_error("Buffer too small");
    uint32_t name_len_net;
    memcpy(&name_len_net, data + pos, 4);
    uint32_t name_len = ntohl(name_len_net);
    pos += 4;

    if (pos + name_len > size)
      throw std::runtime_error("Invalid name length");
    std::string dept_name(data + pos, name_len);
    pos += name_len;

    Department dept(dept_name);

    // 2. Leer vector de empleados
    if (pos + 4 > size)
      throw std::runtime_error("Buffer too small");
    uint32_t num_employees_net;
    memcpy(&num_employees_net, data + pos, 4);
    uint32_t num_employees = ntohl(num_employees_net);
    pos += 4;

    // Leer cada empleado
    for (uint32_t i = 0; i < num_employees; i++) {
      // Leer el tamaño del empleado
      if (pos + 4 > size)
        throw std::runtime_error("Buffer too small");
      uint32_t emp_size_net;
      memcpy(&emp_size_net, data + pos, 4);
      uint32_t emp_size = ntohl(emp_size_net);
      pos += 4;

      // Leer los datos del empleado
      if (pos + emp_size > size)
        throw std::runtime_error("Buffer too small");
      Employee emp = Employee::from_bytes(data + pos, emp_size);
      dept.employees.push_back(emp);
      pos += emp_size;
    }

    return dept;
  }

  void print() const {
    std::cout << "Department{name=\"" << dept_name << "\", employees=[\n";
    for (size_t i = 0; i < employees.size(); i++) {
      std::cout << "  [" << i << "] ";
      employees[i].print();
      std::cout << "\n";
    }
    std::cout << "]}";
  }
};

// ============================================================================
// SERVIDOR
// ============================================================================
void run_server(uint16_t port) {
  // Crear socket
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Error creando socket\n";
    return;
  }

  // Permitir reutilizar el puerto inmediatamente
  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // Configurar dirección
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  // Bind
  if (bind(server_fd, (sockaddr *)&address, sizeof(address)) < 0) {
    std::cerr << "Error en bind\n";
    close(server_fd);
    return;
  }

  // Listen
  listen(server_fd, 1);
  std::cout << "🚀 Servidor escuchando en puerto " << port << "...\n\n";

  // Accept
  int client_fd = accept(server_fd, nullptr, nullptr);
  if (client_fd < 0) {
    std::cerr << "Error en accept\n";
    close(server_fd);
    return;
  }

  std::cout << "✅ Cliente conectado!\n\n";

  // Primero recibir el tamaño del mensaje
  uint32_t msg_size_net;
  recv(client_fd, &msg_size_net, 4, MSG_WAITALL);
  uint32_t msg_size = ntohl(msg_size_net);

  std::cout << "📦 Esperando " << msg_size << " bytes...\n";

  // Recibir los datos
  std::vector<char> buffer(msg_size);
  size_t total_received = 0;
  while (total_received < msg_size) {
    ssize_t bytes = recv(client_fd, buffer.data() + total_received,
                         msg_size - total_received, 0);
    if (bytes <= 0)
      break;
    total_received += bytes;
  }

  std::cout << "📥 Recibidos " << total_received << " bytes\n\n";

  // Deserializar
  Department dept = Department::from_bytes(buffer.data(), buffer.size());

  // Imprimir
  std::cout << "🏢 Departamento recibido:\n";
  std::cout << "════════════════════════════════════════\n";
  dept.print();
  std::cout << "\n════════════════════════════════════════\n";

  close(client_fd);
  close(server_fd);
}

// ============================================================================
// CLIENTE
// ============================================================================
void run_client(const char *ip, uint16_t port) {
  // Crear socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    std::cerr << "Error creando socket\n";
    return;
  }

  // Configurar dirección del servidor
  sockaddr_in serv_addr{};
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  inet_pton(AF_INET, ip, &serv_addr.sin_addr);

  // Conectar
  if (connect(sock, (sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    std::cerr << "Error conectando\n";
    close(sock);
    return;
  }

  std::cout << "✅ Conectado al servidor!\n\n";

  // Crear un departamento con datos de ejemplo
  Department dept("Ingeniería de Software");

  // Empleado 1: con oficina
  Person p1(101, "Ana García", 28);
  Employee emp1(p1, 75000);
  emp1.office_location = new Point(3, 15); // Piso 3, Oficina 15
  dept.employees.push_back(emp1);

  // Empleado 2: sin oficina (remoto)
  Person p2(102, "Carlos López", 35);
  Employee emp2(p2, 85000);
  // emp2.office_location = nullptr (ya es nullptr por defecto)
  dept.employees.push_back(emp2);

  // Empleado 3: con oficina
  Person p3(103, "María Rodríguez", 42);
  Employee emp3(p3, 95000);
  emp3.office_location = new Point(2, 8); // Piso 2, Oficina 8
  dept.employees.push_back(emp3);

  std::cout << "📤 Enviando departamento:\n";
  std::cout << "════════════════════════════════════════\n";
  dept.print();
  std::cout << "\n════════════════════════════════════════\n\n";

  // Serializar
  std::vector<char> buffer = dept.to_bytes();

  // Enviar primero el tamaño
  uint32_t msg_size = buffer.size();
  uint32_t msg_size_net = htonl(msg_size);
  send(sock, &msg_size_net, 4, 0);

  // Enviar los datos
  send(sock, buffer.data(), buffer.size(), 0);

  std::cout << "✅ Enviados " << buffer.size() << " bytes exitosamente!\n";

  close(sock);
}

// ============================================================================
// MAIN
// ============================================================================
int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Uso:\n"
              << "  " << argv[0] << " server <puerto>\n"
              << "  " << argv[0] << " client <ip> <puerto>\n\n"
              << "Ejemplo:\n"
              << "  Terminal 1: " << argv[0] << " server 8080\n"
              << "  Terminal 2: " << argv[0] << " client 127.0.0.1 8080\n";
    return 1;
  }

  std::string mode = argv[1];
  if (mode == "server" && argc == 3) {
    run_server(std::stoi(argv[2]));
  } else if (mode == "client" && argc == 4) {
    run_client(argv[2], std::stoi(argv[3]));
  } else {
    std::cerr << "Argumentos inválidos\n";
    return 1;
  }

  return 0;
}