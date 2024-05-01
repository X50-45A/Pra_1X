#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>


#define SUBS_REQ 0x00
#define SUBS_ACK 0x01
#define SUBS_REJ 0x02
#define SUBS_INFO 0x03
#define INFO_ACK  0x04
#define SUBS_NACK 0x05

#define DISCONNECTED 0xa0
#define NOT_SUBSCRIBED 0xa1
#define WAIT_ACK_SUBS
#define HELLO 0x10
#define HELLO_REJ 0x11
#define UDP_PACKET_SIZE sizeof(struct udp_PDU)
#define MAX_NAME_LENGTH 50
#define MAX_CONTROLLERS 100
#define MAX_CONTROLLER_ID_LENGTH 10
typedef struct args args;
/*variables globals*/
struct Server;
char software_config_file[20] = "server.cfg";
int debug_flag = 0;
char state[30] = "DISCONNECTED";
struct Controller {
    char id[MAX_CONTROLLER_ID_LENGTH];
    char mac[13];
};
struct udp_PDU{
    unsigned char type;
    char mac[13];
    char rand[9];
    char data[80];
};

struct Server {
    char name[20];
    char mac[12];
    int udp_port;
    int tcp_port;
};
struct DataStorage {
    char devices[256];
    int tcp_port;
};
struct AuthorizedController {
    char mac[14]; // Dirección MAC del controlador
    char name[50]; // Nombre del controlador
    char *random_num;
    char *data;
};
struct thread_args {
    int sockfd;
    struct AuthorizedController *controllers;
    struct udp_PDU packet;
    struct sockaddr_in client_addr;
};
struct ThreadArgs2 {
    struct AuthorizedController *controllers;
};
/*--------------------------------------------------*/
/*funcions*/
void handle_signal(int signum);
void close_socket();
void debug(char msg[]);
struct Server leer_configuracion(const char *archivo);
struct Controller * read_controller();
void parse_parameters(int argc,char  **argv);
void send_subs_ack(int sockfd, struct sockaddr_in client_addr, char mac[], char random_num[], struct Server server_config);
void *process_received_packet(void *arg);
char generate_random(char *rand_str, int size);
void send_subs_rej(int sockfd, struct sockaddr_in client_addr, char mac[]);
void save_data(const char *packet, struct DataStorage *data_storage);
void send_hello_ack(int sockfd, struct sockaddr_in client_addr, char mac[], char random_num[]);
void send_hello_rej(int sockfd, struct sockaddr_in client_addr, char mac[]);
void send_info_ack(int sockfd, struct sockaddr_in client_addr, char *mac, char *random_num);
void list_controllers(struct AuthorizedController *controllers);
void set_device_value(char *controller_name, char *device_name, char *value);
void get_device_info(char *controller_name, char *device_name);
void *packet_receiver(void *arg);
void *send_periodic_hello_ack(int sockfd, struct sockaddr_in client_addr, char *mac, char *random_num,char *additional_data);

void process_subs_req(struct udp_PDU packet, struct Controller *controllers, int num_controllers, int sockfd, struct sockaddr_in client_addr);
/*-----------------------------------------------*/
int sockfd;
int running = 1;
struct Server config;
int in_subscription_mode = 1;
int main(int argc, char **argv) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    struct sockaddr_in *client_addr;
    struct AuthorizedController *controllers;
    parse_parameters(argc, argv);
    config = leer_configuracion(software_config_file);
    debug("S'ha llegit l'arxiu de configuració");
    printf("La configuració llegida és la següent: \n \t Name: %s \n \t MAC: %s \n \t Udp: %d \n \t Tcp: %i \n",
           config.name, config.mac, config.udp_port, config.tcp_port);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("Error al crear el socket UDP");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config.udp_port);
    if (bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("Error al vincular el socket a la dirección del servidor");
        exit(EXIT_FAILURE);
    }
    pthread_t thread;
    controllers = (struct AuthorizedController *) read_controller();
    if (pthread_create(&thread, NULL, packet_receiver, (void *) controllers) !=
        0) { // Pasando controllers como argumento
        perror("Error al crear el hilo");
        close_socket();
        exit(EXIT_FAILURE);
    }

    printf("Servidor en ejecución. Presione Ctrl+C para detener.\n");


    char input[100];
    char command[20], arg1[MAX_NAME_LENGTH], arg2[MAX_NAME_LENGTH], arg3[MAX_NAME_LENGTH];

    printf("Consola de sistema del servidor\n");
    printf("Ingrese un comando ('help' para ver la lista de comandos):\n");

    while (running) {
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0;
        sscanf(input, "%s %s %s %s", command, arg1, arg2, arg3);

        if (strcmp(command, "list") == 0) {
            list_controllers(controllers);
            continue;
        } else if (strcmp(command, "set") == 0) {
            // Implementación de la lógica para el comando set
            // set_device_value(arg1, arg2, arg3);
        } else if (strcmp(command, "get") == 0) {
            // Implementación de la lógica para el comando get
            // get_device_info(arg1, arg2);
        } else if (strcmp(command, "quit") == 0) {
            printf("Finalizando el servidor...\n");
            running = 0;
            break;
        } else if (strcmp(command, "help") == 0) {

            printf("Lista de comandos disponibles:\n");
            printf("  list\n");
            printf("  set <nombre_controlador> <nombre_dispositivo> <valor>\n");
            printf("  get <nombre_controlador> <nombre_dispositivo>\n");
            printf("  quit\n");
        } else {
            printf("Comando no reconocido. Escriba 'help' para ver la lista de comandos.\n");
        }

        //console_input_handler(controllers);

    }
    close_socket();
    return 0;
}


void list_controllers(struct AuthorizedController *controllers) {
    printf("Lista de controladores autorizados:\n");
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        if (strcmp(controllers[i].mac, "") != 0) {
            printf("Controlador: %s, Estado: %s\n", controllers[i].name, state);

            if (strcmp(state, "SUSCRITO") == 0) {

                printf("   - IP: %s\n", "127.0.0.1"); // Ejemplo de dirección IP
                printf("   - Número aleatorio: %s\n", controllers[i].random_num); // Mostrar número aleatorio
                printf("   - Situación: %s\n", "Suscrito"); // Mostrar estado suscrito


                char *data = controllers[i].data;
                char *token = strtok(data, ",");
                int count = 0;
                while (token != NULL) {
                    if (count == 0) {
                        printf("   - Elementos: %s\n", token); // Mostrar la cantidad de elementos
                    } else {
                        printf("     - Elemento %d: %s\n", count, token); // Mostrar cada elemento
                    }
                    token = strtok(NULL, ",");
                    count++;
                }
            }
        }
    }
}

struct Server leer_configuracion(const char *archivo) {
    struct Server server;

    FILE *file = fopen(archivo, "r");
    if (file == NULL) {
        perror("Error al abrir el archivo");
        exit(EXIT_FAILURE);
    }

    // Leer y almacenar los datos
    fscanf(file, "Name = %s\n", server.name);
    fscanf(file, "MAC = %s\n", server.mac);
    fscanf(file, "UDP-port = %d\n", &server.udp_port);
    fscanf(file, "TCP-port = %d\n", &server.tcp_port);

    fclose(file);
    return server;
}
void debug(char msg[]) {
    if (debug_flag == 1) {
        time_t _time = time(0);
        struct tm *tlocal = localtime(&_time);
        char output[128];
        strftime(output, 128, "%H:%M:%S", tlocal);
        printf("%s: DEBUG -> %s \n", output, msg);
    }
}


void parse_parameters(int argc, char **argv) {
    int i;
    if (argc > 1) {
        for (i = 0; i < argc; i++) {               /* PARSING PARAMETERS */
            char const *option = argv[i];
            if (option[0] == '-') {
                switch (option[1]) {
                    case 'd':
                        debug_flag = 1;
                        break;
                    case 'c':
                        strcpy(software_config_file, argv[i + 1]);
                        break;
                    default:
                        fprintf(stderr, "Wrong parameters Input \n");
                        exit(-1);
                }
            }
        }
    }
    debug("S'ha seleccionat l'opció debug");
}

char generate_random(char *rand_str, int size) {

    srand(time(NULL));

    for (int i = 0; i < size - 1; i++) {
        rand_str[i] = rand() % 10 + '3';
    }
    rand_str[size - 1] = '\0';
    return *rand_str;
}
void send_subs_rej(int sockfd, struct sockaddr_in client_addr, char mac[]) {
    // Crear un paquete SUBS_REJ
    struct udp_PDU packet;
    char num[9];
    generate_random(num, sizeof(num));
    packet.type = SUBS_REJ; // Tipo de paquete SUBS_REJ
    strncpy(packet.mac, mac, sizeof(packet.mac) - 1);
    strncpy(packet.rand, num, sizeof(packet.rand) - 1);
    strncpy(packet.data, "Motivo del rechazo: Incorrecto o inexistente.", sizeof(packet.data) - 1);

    // Enviar el paquete SUBS_REJ al cliente
    ssize_t bytes_sent = sendto(sockfd, &packet, UDP_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
    if (bytes_sent == -1) {
        perror("Error al enviar el paquete SUBS_REJ al cliente");
        exit(EXIT_FAILURE);
    }
}

void process_subs_req(struct udp_PDU packet, struct Controller *controllers, int num_controllers, int sockfd, struct sockaddr_in client_addr) {
    int i;
    char rand[9];
    *rand = generate_random(rand,9);
    debug("Chequeo de autorización.\n");
    for (i = 0; i < num_controllers; i++) {
        if (strcmp(packet.mac, controllers[i].mac) == 0) {
            // Si el controlador está autorizado, envía un paquete SUBS_ACK
            send_subs_ack(sockfd, client_addr, packet.mac, rand,config);
            char name[MAX_NAME_LENGTH];
            char situation[MAX_NAME_LENGTH];
            char elements[MAX_NAME_LENGTH];
            int local_tcp;
            char server[MAX_NAME_LENGTH];
            int srv_udp;
            sscanf(packet.data, "Name = %[^;]; Situation = %[^;]; Elements = %[^;]; MAC = %*s; Local-TCP = %d; Server = %[^;]; Srv-UDP = %d", name, situation, elements, &local_tcp, server, &srv_udp);

            // Configurar la dirección del cliente
            memset(&client_addr, 0, sizeof(client_addr));
            client_addr.sin_family = AF_INET;
            client_addr.sin_addr.s_addr = inet_addr(server);
            client_addr.sin_port = htons(srv_udp);

            return;
        }
    }
    // Si el controlador no está autorizado, envía un paquete SUBS_REJ
    send_subs_rej(sockfd, client_addr, packet.mac);
}


void send_subs_ack(int sockfd, struct sockaddr_in client_addr, char mac[], char random_num[], struct Server server_config) {
    struct udp_PDU packet;
    packet.type = SUBS_ACK;
    strncpy(packet.mac, mac, sizeof(packet.mac));
    strncpy(packet.rand, random_num, sizeof(packet.rand));


    char data[MAX_NAME_LENGTH * 3 + 20];
    sprintf(data, "%d",server_config.udp_port);


    strncpy(packet.data, data, sizeof(packet.data));

    ssize_t bytes_sent = sendto(sockfd, &packet, UDP_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
    debug("S'ha enviat el packet SUBS_ACK.\n");
    if (bytes_sent == -1) {
        perror("Error al enviar el paquete SUBS_ACK al cliente");
        exit(EXIT_FAILURE);
    }
}


void save_data(const char *packet, struct DataStorage *data_storage) {
    // Copia el campo "data" de la PDU en la estructura de almacenamiento
    sscanf(packet, "%d,%255[^;]", &data_storage->tcp_port, data_storage->devices);
}
void send_info_ack(int sockfd, struct sockaddr_in client_addr, char *mac, char *random_num) {
    struct udp_PDU packet;

    packet.type = INFO_ACK;
    strncpy(packet.mac, mac, sizeof(packet.mac));
    strncpy(packet.rand, random_num, sizeof(packet.rand));


    char data[80];
    sprintf(data, "%d", config.tcp_port);

    strncpy(packet.data, data, sizeof(data));

    ssize_t bytes_sent = sendto(sockfd, &packet, UDP_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
    debug("S'ha enviat el packet INFO_ACK.\n");
    if (bytes_sent == -1) {
        perror("Error al enviar el paquete INFO_ACK al cliente");
        exit(EXIT_FAILURE);
    }
}



void *packet_receiver(void *arg) {
    struct udp_PDU packet;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    struct AuthorizedController *controllers = (struct AuthorizedController *)arg;

    char buffer[UDP_PACKET_SIZE];

    while (running) {
        ssize_t bytes_received = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (bytes_received == -1) {
            perror("Error al recibir el paquete UDP del cliente");
            exit(EXIT_FAILURE);
        }


        strcpy(packet.mac, buffer + 1);
        strcpy(packet.rand, buffer + 14);
        strcpy(packet.data, buffer + 23);
        packet.type = (unsigned char)buffer[0];


        struct thread_args *args = malloc(sizeof(struct thread_args));
        if (args == NULL) {
            perror("Error al asignar memoria para los argumentos de la función process_received_packet");
            exit(EXIT_FAILURE);
        }
        args->sockfd = sockfd;
        args->controllers = controllers;
        args->packet = packet;
        args->client_addr = client_addr;

        pthread_t packet_thread;
        if (pthread_create(&packet_thread, NULL, process_received_packet, args) != 0) {
            perror("Error al crear el hilo para procesar el paquete recibido");
            exit(EXIT_FAILURE);
        }


        if (packet.type == INFO_ACK) {
            in_subscription_mode = 0;
        }
    }
    return NULL;
}




void *process_received_packet(void *arg) {
    struct thread_args *args = (struct thread_args *)arg;
    int sockfd = args->sockfd;
    struct AuthorizedController *controllers = args->controllers; // Cambiar el tipo de datos a struct AuthorizedController *
    struct udp_PDU packet = args->packet;
    struct sockaddr_in client_addr = args->client_addr;

    struct DataStorage data_storage;
    switch (packet.type) {
        case SUBS_REQ: // SUBS_REQ
            debug("Se recibió el paquete SUBS_REQ del Cliente.\n");

            int num_controllers = MAX_CONTROLLERS;
            process_subs_req(packet, (struct Controller *)controllers, num_controllers, sockfd, client_addr); // Cambiar el tipo de datos a struct Controller *
            break;
        case SUBS_INFO: // SUBS_INFO
            debug("Se recibió el paquete SUBS_INFO del Cliente.\n");
            save_data((const char *)&packet, &data_storage);
            send_info_ack(sockfd, client_addr, packet.mac, packet.rand);
            break;
        case HELLO:
            send_periodic_hello_ack(sockfd,client_addr,packet.mac,packet.rand,packet.data);

            break;
        case HELLO_REJ:
            send_hello_rej(sockfd, client_addr, packet.mac);
            break;
        default:
            // Paquete desconocido
            break;
    }

    free(args); // Liberar la memoria asignada para los argumentos

    return NULL;
}

void *send_periodic_hello_ack(int sockfd, struct sockaddr_in client_addr, char *mac, char *random_num,char *additional_data) {

    char rand[9];
    *rand = generate_random(rand,9);
    struct udp_PDU packet;

    packet.type = HELLO;
    strncpy(packet.mac, mac, sizeof(packet.mac));
    strncpy(packet.rand, random_num, sizeof(packet.rand));


    char data[80];
    sprintf(data, "%s",additional_data);

    strncpy(packet.data, data, sizeof(data));

        // Enviar el paquete HELLO_ACK periódicamente
        ssize_t bytes_sent = sendto(sockfd, &packet, UDP_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
        if (bytes_sent == -1) {
            perror("Error al enviar el paquete [HELLO_ACK]");
        }


}

void send_hello_rej(int sockfd, struct sockaddr_in client_addr, char mac[]) {
    struct udp_PDU rej_packet;
    rej_packet.type = HELLO_REJ;
    strncpy(rej_packet.mac, mac, sizeof(rej_packet.mac));

    ssize_t bytes_sent = sendto(sockfd, &rej_packet, UDP_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
    if (bytes_sent == -1) {
        perror("Error al enviar el paquete [HELLO_REJ]");
    }
}

struct Controller * read_controller(){
    FILE *file;
    char line[100];
    struct Controller *controllers = malloc(MAX_CONTROLLERS * sizeof(struct Controller));
    int count = 0;

    // Verificar si la asignación de memoria fue exitosa
    if (controllers == NULL) {
        perror("Error al asignar memoria para los controladores");
        exit(EXIT_FAILURE);
    }

    // Abrir el archivo para lectura
    file = fopen("controllers.dat", "r");
    if (file == NULL) {
        perror("Error al abrir el archivo");
        exit(EXIT_FAILURE);
    }

    // Leer cada línea del archivo
    while (fgets(line, sizeof(line), file)) {
        // Separar el ID y la MAC utilizando la coma como delimitador
        char *id = strtok(line, ",");
        char *mac = strtok(NULL, ",");

        // Verificar si se alcanzó el final del archivo o si se excedió el límite de controladores
        if (mac == NULL || count >= MAX_CONTROLLERS) {
            break;
        }

        // Copiar el ID y la MAC a la estructura del controlador
        strcpy(controllers[count].id, id);
        strncpy(controllers[count].mac, mac, sizeof(controllers[count].mac) - 1);

        count++;
    }

    // Cerrar el archivo
    fclose(file);

    // Imprimir la información de los controladores
    printf("Controladores leídos del archivo:\n");
    for (int i = 0; i < count; i++) {
        printf("ID: %s, MAC: %s\n", controllers[i].id, controllers[i].mac);
    }

    return controllers;
}



void set_device_value(char *controller_name, char *device_name, char *value) {
    printf("Simulando envío de información al dispositivo %s en el controlador %s. Nuevo valor: %s\n", device_name, controller_name, value);
}


void get_device_info(char *controller_name, char *device_name) {
    printf("Simulando solicitud de información del dispositivo %s en el controlador %s\n", device_name, controller_name);
}
void handle_signal(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("Se recibió la señal para finalizar el servidor.\n");
        running = 0;
    }
}

void close_socket() {
    if (sockfd != -1) {
        close(sockfd);
        sockfd = -1;
    }
}