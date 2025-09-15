📚 Guía Completa: Manipulación de Archivos en C++
📖 Índice
Diferencia entre Archivos Binarios y Texto

Funciones para Guardar Historial

Tutorial de Lectura de Archivos

Conversiones y Serialización

Ejemplo Práctico con Texto

Tabla Comparativa

Consejos Importantes

📝 Diferencia entre Archivos Binarios y Texto
Archivos de Texto
Formato: Legible por humanos

Ejemplo: "Hola mundo\n2024-01-15 10:30:25\n"

Ventajas: Fáciles de leer y debuggear

Desventajas: Mayor tamaño, más lentos

Archivos Binarios
Formato: Bytes crudos

Ejemplo: 0x48 0x6F 0x6C 0x61 0x00 0x64 0x61 0x74 0x61

Ventajas: Eficientes, rápidos, tamaño reducido

Desventajas: No legibles directamente

🛠️ Funciones para Guardar Historial
Función para Archivo Binario
cpp
void guardarMensajeHistorial(const string& tipo, const string& remitente, 
                            const string& destinatario, const string& mensaje) {
    ofstream archivo("historial_chat.bin", ios::binary | ios::app);
    
    if (archivo) {
        // 1. Timestamp (4 bytes)
        time_t ahora = time(nullptr);
        archivo.write((char*)&ahora, sizeof(ahora));
        
        // 2. Tipo de mensaje (1 byte)
        char tipoChar = tipo[0];
        archivo.write(&tipoChar, 1);
        void guardarMensajeHistorial(const string& tipo, const string& remitente, 
                            const string& destinatario, const string& mensaje) {
    ofstream archivo("historial_chat.bin", ios::binary | ios::app);
    
    if (archivo) {
        // 1. Timestamp (4 bytes)
        time_t ahora = time(nullptr);
        archivo.write((char*)&ahora, sizeof(ahora));
        
        // 2. Tipo de mensaje (1 byte)
        char tipoChar = tipo[0];
        archivo.write(&tipoChar, 1);
        
        // 3. Remitente (2 bytes longitud + texto)
        uint16_t longRemitente = remitente.size();
        archivo.write((char*)&longRemitente, sizeof(longRemitente));
        archivo.write(remitente.c_str(), longRemitente);
        
        // 4. Destinatario (2 bytes longitud + texto)
        uint16_t longDestinatario = destinatario.size();
        archivo.write((char*)&longDestinatario, sizeof(longDestinatario));
        archivo.write(destinatario.c_str(), longDestinatario);
        
        // 5. Mensaje (4 bytes longitud + texto)
        uint32_t longMensaje = mensaje.size();
        archivo.write((char*)&longMensaje, sizeof(longMensaje));
        archivo.write(mensaje.c_str(), longMensaje);
    }
}
        // 3. Remitente (2 bytes longitud + texto)
        uint16_t longRemitente = remitente.size();
        archivo.write((char*)&longRemitente, sizeof(longRemitente));
        archivo.write(remitente.c_str(), longRemitente);
        
        // 4. Destinatario (2 bytes longitud + texto)
        uint16_t longDestinatario = destinatario.size();
        archivo.write((char*)&longDestinatario, sizeof(longDestinatario));
        archivo.write(destinatario.c_str(), longDestinatario);
        
        // 5. Mensaje (4 bytes longitud + texto)
        uint32_t longMensaje = mensaje.size();
        archivo.write((char*)&longMensaje, sizeof(longMensaje));
        archivo.write(mensaje.c_str(), longMensaje);
    }
}
Función para Archivo de Texto (Simplificada)
cpp
void guardarMensajeTexto(const string& tipo, const string& remitente, 
                        const string& destinatario, const string& mensaje) {
    ofstream archivo("historial_chat.txt", ios::app);
    
    if (archivo) {
        time_t ahora = time(nullptr);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&ahora));
        
        archivo << "[" << buffer << "] ";
        
        if (tipo == "M") {
            archivo << "BROADCAST de " << remitente << ": " << mensaje << "\n";
        } else {
            archivo << "PRIVADO de " << remitente << " para " << destinatario << ": " << mensaje << "\n";
        }
    }
}
📖 Tutorial de Lectura de Archivos
Lectura de Archivos de Texto
cpp
void leerArchivoTexto() {
    ifstream archivo("historial_chat.txt");
    
    if (!archivo) {
        cout << "No se pudo abrir el archivo" << endl;
        return;
    }
    
    string linea;
    while (getline(archivo, linea)) {
        cout << linea << endl;
    }
    
    archivo.close();
}
Lectura de Archivos Binarios (Paso a Paso)
cpp
void leerArchivoBinario() {
    ifstream archivo("historial_chat.bin", ios::binary);
    
    if (!archivo) {
        cout << "No se pudo abrir el archivo" << endl;
        return;
    }
    
    // Mover al final para saber el tamaño
    archivo.seekg(0, ios::end);
    size_t tamaño = archivo.tellg();
    archivo.seekg(0, ios::beg);
    
    cout << "Tamaño del archivo: " << tamaño << " bytes" << endl;
    
    while (archivo) {
        // 1. Leer timestamp (4 bytes)
        time_t fecha;
        archivo.read((char*)&fecha, sizeof(fecha));
        if (!archivo) break;
        
        // 2. Leer tipo (1 byte)
        char tipo;
        archivo.read(&tipo, 1);
        
        // 3. Leer longitud del remitente (2 bytes)
        uint16_t longRemitente;
        archivo.read((char*)&longRemitente, sizeof(longRemitente));
        
        // 4. Leer remitente (texto)
        string remitente(longRemitente, ' ');
        archivo.read(&remitente[0], longRemitente);
        
        // 5. Leer longitud del destinatario (2 bytes)
        uint16_t longDestinatario;
        archivo.read((char*)&longDestinatario, sizeof(longDestinatario));
        
        // 6. Leer destinatario (texto)
        string destinatario(longDestinatario, ' ');
        archivo.read(&destinatario[0], longDestinatario);
        
        // 7. Leer longitud del mensaje (4 bytes)
        uint32_t longMensaje;
        archivo.read((char*)&longMensaje, sizeof(longMensaje));
        
        // 8. Leer mensaje (texto)
        string mensaje(longMensaje, ' ');
        archivo.read(&mensaje[0], longMensaje);
        
        // Mostrar los datos
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&fecha));
        
        cout << "[" << buffer << "] ";
        if (tipo == 'M') {
            cout << "BROADCAST de " << remitente << ": " << mensaje << endl;
        } else {
            cout << "PRIVADO de " << remitente << " para " << destinatario << ": " << mensaje << endl;
        }
    }
    
    archivo.close();
}
🔄 Conversiones y Serialización
De Número a Bytes (Serialización)
cpp
// Entero a bytes
int numero = 12345;
archivo.write((char*)&numero, sizeof(numero));

// String a bytes
string texto = "Hola";
uint16_t longitud = texto.size();
archivo.write((char*)&longitud, sizeof(longitud));
archivo.write(texto.c_str(), longitud);
De Bytes a Número (Deserialización)
cpp
// Bytes a entero
int numero;
archivo.read((char*)&numero, sizeof(numero));

// Bytes a string
uint16_t longitud;
archivo.read((char*)&longitud, sizeof(longitud));
string texto(longitud, ' ');
archivo.read(&texto[0], longitud);
Conversión de Endianness (Para redes)
cpp
#include <arpa/inet.h>

// Host to Network (para enviar)
uint32_t numero_red = htonl(numero_local);

// Network to Host (para recibir)
uint32_t numero_local = ntohl(numero_red);
🎯 Ejemplo Práctico con Texto
Modificación para Server.cpp
cpp
// En los casos 'm' y 't', cambia la llamada:
guardarMensajeTexto("M", senderNick, "Todos", msg);
// o
guardarMensajeTexto("T", senderNick, destNick, msg);
Para Leer el Historial en Texto
cpp
case 'h': {
    cout << "Cliente solicitó historial del chat" << endl;
    
    ifstream archivo("historial_chat.txt");
    string historialCompleto;
    string linea;
    
    while (getline(archivo, linea)) {
        historialCompleto += linea + "\n";
    }
    
    if (historialCompleto.empty()) {
        string respuesta = "HNo hay historial disponible";
        write(socketConn, respuesta.c_str(), respuesta.size());
    } else {
        string respuesta = "H" + padNumber(historialCompleto.size(), 6) + historialCompleto;
        write(socketConn, respuesta.c_str(), respuesta.size());
    }
    break;
}
📊 Tabla Comparativa
Característica	Binario	Texto
Tamaño	🟢 Pequeño	🔴 Grande
Velocidad	🟢 Rápido	🔴 Lento
Legibilidad	🔴 Difícil	🟢 Fácil
Debugging	🔴 Complejo	🟢 Simple
Portabilidad	🟢 Mejor	🔴 Menor
💡 Consejos Importantes
✅ Siempre verifica si el archivo se abrió correctamente

✅ Cierra los archivos cuando termines de usarlos

✅ Maneja errores con try-catch cuando trabajes con archivos

✅ Usa ios::binary solo cuando necesites manipular bytes crudos

✅ Para texto simple, es mejor usar archivos de texto normales

🚀 Ejercicio Práctico
Intenta implementar ambas versiones (binario y texto) y compara:

El tamaño de los archivos generados

La velocidad de lectura/escritura

La facilidad de debugging