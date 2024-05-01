import argparse
import struct
import sys, os, traceback, optparse
import threading
import time, datetime
import socket

from dataclasses import dataclass


@dataclass(frozen=True)
class Constantes:
    # Constants fase de registre
    SUBS_REQ = 0x00
    SUBS_ACK = 0x01
    SUBS_REJ = 0x02
    SUBS_INFO = 0x03
    INFO_ACK = 0x04
    SUBS_NACK = 0x05
    TIMEOUT = 0x06
    # Constant de estats de un client
    DISCONNECTED = 0xa0
    NOT_SUBSCRIBED = 0xa1
    WAIT_ACK_SUBS = 0xa2
    WAIT_INFO = 0xa3
    WAIT_ACK_INFO = 0xa4
    SUBSCRIBED = 0xa5
    SEND_HELLO = 0xa6

    HELLO = 0x10
    HELLO_REJ = 0x11


class Client:
    def __init__(self, name, situation, elements, mac, localTCP, server, servUDP):
        self.name = name
        self.situation = situation
        self.elements = elements
        self.mac = mac
        self.localTCP = localTCP
        self.server = server
        self.servUDP = servUDP
        self.state = Constantes.DISCONNECTED


def create_sockets():
    global sockTCP, sockUDP
    sockTCP = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    debug("Inicializado el socket TCP")
    sockUDP = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    debug("Inicializado el socket UDP")


def debug(msg):
    if debugs:
        print(time.strftime("%H:%M:%S") + ": DEBUG -> " + str(msg))


def leer_archivo(archivo):
    with open(archivo, 'r') as file:
        name = file.readline().strip('\n').split(' ')[2]
        situation = file.readline().strip('\n').split(' ')[2]
        elements = file.readline().strip('\n').split(' ')[2]
        mac = file.readline().strip('\n').split(' ')[2]
        localTCP = file.readline().strip('\n').split(' ')[2]
        server = file.readline().strip('\n').split(' ')[2]
        servUDP = file.readline().strip('\n').split(' ')[2]

    debug_str = 'Leido config client' + name + 'con la MAC siguiente:' + mac
    debug(debug_str)
    if server == 'localhost':
        server = '127.0.0.1'
    return Client(name, situation, elements, mac, localTCP, server, int(servUDP))


def get_packet_information(data_string):
    trama = []
    packet = {'type': 0x00, 'MAC': "", 'random': "", 'data': ""}

    for element in data_string:
        trama.append(str(element).split('\x00')[0])
    hex_string = trama[0]
    packet['type'] = int.from_bytes(eval(hex_string), 'big')
    packet['MAC'] = trama[1]
    packet['random'] = trama[2]
    packet['data'] = trama[3]

    return packet


def listen_udp():
    print(client.server, client.servUDP)

    debug("Escuchando paquete UDP")

    data, addr = sockUDP.recvfrom(struct.calcsize(udp_format))
    data_string = struct.unpack(udp_format, data)

    packet = get_packet_information(data_string)
    size = sys.getsizeof(data)
    debug_string = 'Recived: ' + str(size) + ' Bytes ' + ' type: ' + str(packet['type']) + ' mac: ' + str(
        packet['MAC']) + ' random: ' + str(eval(packet['random']).decode("utf-8").strip('\x00')) + ' data: ' + str(
        packet['data'])
    debug(debug_string)
    return packet


def send_sub_req(addr):
    trama = struct.pack(udp_format, bytes([Constantes.SUBS_REQ]), str(client.mac).encode(), b"00000000",
                        str(f"{client.name},{client.situation}").encode())
    sent = sockUDP.sendto(trama, addr)
    debug_string = 'Enviado ' + str(sent) + ' Bytes ' + ' Tipo: ' + str(
        Constantes.SUBS_REQ) + ' nombre: ' + client.name + ' mac: ' + client.mac + ' numero aleatorio: 00000000'
    debug(debug_string)


def send_info_sub(addr, packet):
    controller_MAC = packet['MAC'].replace('\\x00', '')

    random_number = int(packet['random'].replace('\\x00', '').strip("b'"))

    tcp_port = client.localTCP  # Extraer el puerto TCP

    new_udp = int(packet['data'].strip("\"b'").split("\\x00")[0])  # sacar el nuevo udp
    devices_list = client.elements

    trama = struct.pack(udp_format, bytes([Constantes.SUBS_INFO]), str(client.mac).encode(),
                        str(random_number).encode(),
                        str(f"{tcp_port},{devices_list}").encode())
    sent = sockUDP.sendto(trama, (client.server, new_udp))
    debug_string = 'Enviado ' + str(sent) + ' Bytes ' + ' Tipo: ' + str(
        Constantes.SUBS_INFO) + ' ControlMac: ' + client.mac + ' NumeroAleatorio: ' + str(random_number) + str(
        tcp_port) + str(devices_list)
    debug(debug_string)


def wait_ack_sub():
    packet = listen_udp()

    if packet and packet['type'] == Constantes.SUBS_ACK:
        return Constantes.SUBS_ACK, packet
    return None, None


def wait_ack_info():
    packet = listen_udp()
    if packet['type'] == Constantes.INFO_ACK:
        return Constantes.INFO_ACK, packet
    return None, None


def subs_proces(addr):
    global packet
    t = 1  # tiempo inicial entre paquetes
    u = 2  # tiempo de espera entre procesos de subscripción
    n = 7  # número de paquetes antes de esperar u segundos
    o = 3  # número de procesos de subscripción antes de finalizar
    p = 3  # número inicial de paquetes con intervalo t
    q = 3  # factor de incremento del intervalo de tiempo
    sent_packets = 0
    sub_attempt = 0
    address = (addr, int(client.servUDP))
    client.state = Constantes.NOT_SUBSCRIBED
    while client.state != Constantes.SUBSCRIBED and sub_attempt < o:
        time_between_package = t

        while sent_packets < n:
            send_sub_req(address)
            client.state = Constantes.WAIT_ACK_SUBS
            wait_rep = True
            while wait_rep:
                response, packet = wait_ack_sub()
                if response == Constantes.SUBS_ACK:
                    client.state = Constantes.WAIT_ACK_INFO
                    wait_rep = False
                elif response == Constantes.TIMEOUT:
                    debug("No se recibió respuesta del servidor. Reenviando [SUBS_REQ].")
                    wait_rep = False

                if sent_packets < p:
                    time_between_package = min(time_between_package + 1, q * t)
            if client.state == Constantes.WAIT_ACK_INFO:
                send_info_sub(address, packet)
                response, packet = wait_ack_info()

                if response == Constantes.INFO_ACK:

                    client.state = Constantes.SUBSCRIBED
                    break
                else:
                    client.state = Constantes.NOT_SUBSCRIBED
            else:
                client.state = Constantes.NOT_SUBSCRIBED

            sent_packets += 1

        if client.state != Constantes.SUBSCRIBED:
            sub_attempt += 1

            if sub_attempt < o:
                debug(
                    f"No se ha completado el proceso de subscripción. Esperando {u} segundos antes de iniciar un nuevo proceso.")
                time.sleep(u)
            else:
                debug(
                    f"No se ha completado el proceso de subscripción después de {o} intentos. No se pudo contactar con el servidor.")
    return packet


def send_hello(addr,packet):
    random_number = int(packet['random'].replace('\\x00', '').strip("b'"))
    controller = client.name
    situacion = client.situation
    trama = struct.pack(udp_format, bytes([Constantes.HELLO]), str(client.mac).encode(), str(random_number).encode(),
                        str(f"{controller},{situacion}").encode())
    sent = sockUDP.sendto(trama, addr)
    debug_string = 'Enviado ' + str(sent) + ' Bytes ' + ' Tipo: ' + str(
        Constantes.HELLO) + ' nombre: ' + client.name + ' mac: ' + str(client.mac) + ' número aleatorio: ' + str(random_number)
    debug(debug_string)


def send_hello_rej(addr,packet):
    random_number = int(packet['random'].replace('\\x00', '').strip("b'"))
    trama = struct.pack(udp_format, bytes([Constantes.HELLO]), str(client.mac).encode(), str(random_number).encode(),
                        b"")
    sent = sockUDP.sendto(trama, addr)
    debug_string = 'Enviado ' + str(sent) + ' Bytes ' + ' Tipo: ' + str(Constantes.HELLO_REJ)
    debug(debug_string)


def print_controller_info():
    print(f"MAC del controlador: {client.mac}")
    print(f"Nombre del controlador: {client.name}")
    print(f"Situación del controlador: {client.situation}")
    print("Dispositivos:")
    for device in client.elements.split(','):
        print(f"- {device}")


def set_device_value(device_name, value):
    print(f"Simulando lectura del dispositivo {device_name}. Cambiando el valor a: {value}")


def send_device_value(device_name):
    print(f"Enviando valor del dispositivo {device_name} al servidor.")


def handle_command(command):
    args = command.split()
    if args[0] == "stat":
        print(f"MAC del controlador: {client.mac}")
        print(f"Nombre del controlador: {client.name}")
        print(f"Situación del controlador: {client.situation}")
        print("Dispositivos:")
        for device in client.elements.split(','):
            print(f"- {device}")
    elif args[0] == "set":
        if len(args) >= 3:
            set_device_value(args[1], args[2])
        else:
            print("Error: Falta el nombre del dispositivo o el valor.")
    elif args[0] == "send":
        if len(args) >= 2:
            send_device_value(args[1])
        else:
            print("Error: Falta el nombre del dispositivo.")
    elif args[0] == "quit":
        print("Finalizando el cliente...")

        sockTCP.close()
        sockUDP.close()

        sys.exit(0)
    else:
        print("Comando no reconocido. Utilice 'help' para ver la lista de comandos.")


def console_loop():
    while True:
        command = input("Ingrese un comando: ")
        handle_command(command)


def periodic_communication(addr, interval,packet):
    while client.state == Constantes.SUBSCRIBED:
        send_hello(addr,packet)
        debug("Esperando respuesta del servidor...")
        packet = listen_udp()

        if packet and packet['type'] == Constantes.HELLO:
            debug("Paquete HELLO recibido del servidor.")
            print(server_mac,packet['MAC'])
            print(packet['random'], server_random)
            print(packet['data'].strip("\"b'").split("\\")[0],f"{client.name},{client.situation}")
            if packet['MAC'] == server_mac.split("\\")[0] and int(packet['random'].strip("\"b'").split("\\")[0]) == server_random and packet['data'].strip("\"b'").split("\\")[0] == f"{client.name},{client.situation}".encode():
                debug("Identidad del servidor verificada.")
            else:
                debug("Discrepancia en la identidad del servidor. Enviando paquete HELLO_REJ.")
                send_hello_rej(addr, packet)
                client.state = Constantes.NOT_SUBSCRIBED
        else:
            debug("No se recibió respuesta del servidor.")
            client.state = Constantes.NOT_SUBSCRIBED
        time.sleep(interval)


def maintain_periodic_communication(addr, interval,packet):
    debug(f"Comunicación periódica con el servidor iniciada. Enviando paquete HELLO cada {interval} segundos.")
    t = threading.Thread(target=periodic_communication, args=(addr, interval,packet))
    t.start()


def main():
    global debugs, client, udp_format, server_mac, server_random
    udp_format = "c13s9s80s"
    parser = argparse.ArgumentParser(description='Descripción del programa.')
    parser.add_argument('-d', action='store_true', help='Fer debugs')
    parser.add_argument('-c', metavar='arxiu', help='Arxiu de dades de configuració del programari')

    args = parser.parse_args()

    if args.d:
        debugs = True
        print("Modo debug activado.")

    if args.c:
        archivo = args.c
    else:
        archivo = "client.cfg"
    create_sockets()
    client = leer_archivo(archivo)
    packet = subs_proces(client.server)
    server_mac = packet['MAC'].replace('\\x00', '')
    server_random = int(packet['random'].replace('\\x00', '').strip("b'"))
    # Iniciar el bucle de consola como un subproceso
    console_thread = threading.Thread(target=console_loop)
    console_thread.start()
    # Iniciar la comunicación periódica con el servidor como un subproceso
    communication_thread = threading.Thread(target=maintain_periodic_communication,
                                            args=((client.server, client.servUDP), 2,packet))
    communication_thread.start()

    # Iniciar el proceso de suscripción como un subproceso

    console_thread.join()
    # Esperar a que los subprocesos terminen
    communication_thread.join()


if __name__ == "__main__":
    main()
