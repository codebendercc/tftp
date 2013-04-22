#ifndef TFTP_PACKET_CPP
#define TFTP_PACKET_CPP


using namespace std;

template<class Caller_P>TFTPClient<Caller_P>::TFTPClient(const char* ip, int port) {

    TFTP_Packet packet;

    server_ip = ip;
    server_port = port;

    //- standartines reiksmes

    socket_descriptor = -1;

    //- wsa

#if defined _WIN32 || _WIN64

    /* edited */

    WSADATA wsaData;

    WORD wVersionRequested = MAKEWORD(2, 2);

    int err = WSAStartup(wVersionRequested, &wsaData);

    if (err != 0) {

        throw new ETFTPSocketInitialize;

    }

#endif

}

template<class Caller_P>TFTPClient<Caller_P>::TFTPClient(const char* ip, int port, Caller* caller) {

    TFTP_Packet packet;

    server_ip = ip;
    server_port = port;

    //- standartines reiksmes

    socket_descriptor = -1;
    caller_ = caller;

    //- wsa

#if defined _WIN32 || _WIN64

    /* edited */

    WSADATA wsaData;

    WORD wVersionRequested = MAKEWORD(2, 2);

    int err = WSAStartup(wVersionRequested, &wsaData);

    if (err != 0) {

        throw new ETFTPSocketInitialize;

    }

#endif

}

template<class Caller_P> int TFTPClient<Caller_P>::connectToServer(int port) {

    socket_descriptor = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_descriptor == -1) {

        throw new ETFTPSocketCreate;

    }

    client_address.sin_family = AF_INET;
    client_address.sin_port = htons(server_port); //- taip pat turi buti ir serveryje!    
    client_address.sin_addr.s_addr = inet_addr(this->server_ip);

#if defined _WIN32 || _WIN64
    //memset(client_address.sin_zero, 0, sizeof(client_address.sinzero);
    //- suvienodinam SOCKADDR_IN ir SOCKADDR strukturu dydzius
#endif

    connection = connect(socket_descriptor, (const struct sockaddr *) &client_address, sizeof (client_address));

    if (connection != 0) {
        return -1;

    }


    return 1;

}

template<class Caller_P> int TFTPClient<Caller_P>::sendBuffer(char *buffer) {

    return send(socket_descriptor, buffer, (int) strlen(buffer), 0);

}

template<class Caller_P> int TFTPClient<Caller_P>::sendPacket(TFTP_Packet* packet) {

    return send(socket_descriptor, (char*) packet->getData(), packet->getSize(), 0);

}

template<class Caller_P> bool TFTPClient<Caller_P>::getFile(char* filename, char* destination) {

    TFTP_Packet packet_rrq, packet_ack;
    ofstream file(destination, ifstream::binary);

    char buffer[TFTP_PACKET_DATA_SIZE];

    packet_rrq.createRRQ(filename);

    sendPacket(&packet_rrq);

    int last_packet_no = 1;
    int wait_status;
    int timeout_count = 0;

    while (true) {

        wait_status = waitForPacketData(last_packet_no, TFTP_CLIENT_SERVER_TIMEOUT);

        if (wait_status == TFTP_CLIENT_ERROR_PACKET_UNEXPECTED) {

            received_packet.dumpData();
            file.close();

            return false;

        } else if (wait_status == TFTP_CLIENT_ERROR_TIMEOUT) {

            //- nebesulaukem paketo

            timeout_count++;

            if (timeout_count < 2) { //- kadangi tai pirmas timeout`as, tai bandom pasiusti paskutini ACK

                sendPacket(&packet_ack);
                continue;

            } else {

                file.close();

                return false;

            }

        }

        if (last_packet_no != received_packet.getNumber()) {
            //- paketas kazkur paklydo!

            /* TFTP recognizes only one error condition that does not cause
               termination, the source port of a received packet being incorrect.
               In this case, an error packet is sent to the originating host. */

            /* Taip negali nutikti, nes pas mus naudojamas ACK Lock`as */

        } else {

            //- paketas tvarkoj
            received_packet.dumpData();

            last_packet_no++;

            //- jei tai susitvarkes timeoutinis paketas, tai pasidziaukim ir leiskim tai pakartot
            if (timeout_count == 1) {
                timeout_count = 0;
            }

            if (received_packet.copyData(4, buffer, TFTP_PACKET_DATA_SIZE)) {

                file.write(buffer, received_packet.getSize() - 4);

                //- tirkinam, ar gauti duomenis yra mazesni nei buferio dydis
                //- jei taip, tai sis paketas buvo paskutinis
                //- A data packet of less than 512 bytes signals termination of a transfer.

                if (received_packet.getSize() - 4 < TFTP_PACKET_DATA_SIZE) {

                    /* The host acknowledging the final DATA packet may terminate its side
                       of the connection on sending the final ACK. */

                    packet_ack.createACK((last_packet_no - 1));

                    if (sendPacket(&packet_ack)) {

                        break;

                    }

                } else {

                    //- ne paskutinis, tai siunciam ACK
                    //- Each data packet contains one block of data, and must be acknowledged by 
                    //- an acknowledgment packet before the next packet can be sent.

                    packet_ack.createACK((last_packet_no - 1)); //- siunciam toki paketo numeri, kuri gavom paskutini

                    sendPacket(&packet_ack);


                }

            }

        }

    }

    file.close();

    return true;

}

template<class Caller_P> int TFTPClient<Caller_P>::waitForPacket(TFTP_Packet* packet, int timeout_ms) {

    packet->clear();

    fd_set fd_reader; // soketu masyvo struktura
    timeval connection_timer; // laiko struktura perduodama select()

    connection_timer.tv_sec = timeout_ms / 1000; // s
    connection_timer.tv_usec = 0; // neveikia o.0 timeout_ms; // ms 

    FD_ZERO(&fd_reader);
    // laukiam, kol bus ka nuskaityti
    FD_SET(socket_descriptor, &fd_reader);

    int select_ready = select(socket_descriptor + 1, &fd_reader, NULL, NULL, &connection_timer);

    if (select_ready == -1) {

        return TFTP_CLIENT_ERROR_SELECT;

    } else if (select_ready == 0) {

        return TFTP_CLIENT_ERROR_TIMEOUT;

    }

    //- turim sekminga event`a

    int receive_status;
    sockaddr_in client;
    int len = sizeof (client);
    receive_status = recvfrom(socket_descriptor, (char*) packet->getData(), TFTP_PACKET_MAX_SIZE, 0, (struct sockaddr *) &client, (socklen_t *) & len);
    int port = ntohs(client.sin_port);
    disconnect();
    connectToServer(port);

    if (receive_status == 0) {
        return TFTP_CLIENT_ERROR_CONNECTION_CLOSED;
    }

    if (receive_status == SOCKET_ERROR) {
        return TFTP_CLIENT_ERROR_RECEIVE;
    }

    //- receive_status - gautu duomenu dydis

    packet->setSize(receive_status);

    return TFTP_CLIENT_ERROR_NO_ERROR;

}

template<class Caller_P> bool TFTPClient<Caller_P>::waitForPacketACK(int packet_number, int timeout_ms) {
    TFTP_Packet received_packet;

    if (waitForPacket(&received_packet, timeout_ms)) {

        if (received_packet.isError()) {

            errorReceived(&received_packet);

            return false;

        }

        if (received_packet.isACK()) {


            return true;

        }

        if (received_packet.isData()) {
            return true;
        }
    }
    return true;

}

template<class Caller_P> int TFTPClient<Caller_P>::waitForPacketData(int packet_number, int timeout_ms) {

    int wait_status = waitForPacket(&received_packet, timeout_ms);

    if (wait_status == TFTP_CLIENT_ERROR_NO_ERROR) {

        if (received_packet.isError()) {

            errorReceived(&received_packet);

            return TFTP_CLIENT_ERROR_PACKET_UNEXPECTED;

        }

        if (received_packet.isData()) {

            return TFTP_CLIENT_ERROR_NO_ERROR;

        }

    }

    return wait_status;

}

template<class Caller_P> int TFTPClient<Caller_P>::sendFile(char* filename, char* destination) {

    TFTP_Packet packet_wrq, packet_data;
    ifstream file(filename, ifstream::binary);
    char memblock[TFTP_PACKET_DATA_SIZE];

    if (!file.is_open() || !file.good()) {
        return 1;
    }
    packet_wrq.createWRQ(destination);
    packet_wrq.dumpData();

    int last_packet_no = 0;

    sendPacket(&packet_wrq);
    while (true) {
        if (waitForPacketACK(last_packet_no++, TFTP_CLIENT_SERVER_TIMEOUT)) {
            file.read(memblock, TFTP_PACKET_DATA_SIZE);

            packet_data.createData(last_packet_no, (char*) memblock, file.gcount());

            caller_->tftp_notify(last_packet_no);

            sendPacket(&packet_data);

            if (file.eof()) {
                break;
            }
        } else {
            file.close();
            return 2;

        }

    }

    file.close();
    disconnect();

    return 0;

}

template<class Caller_P> void TFTPClient<Caller_P>::errorReceived(TFTP_Packet* packet) {

    int error_code = packet->getWord(2);

    switch (error_code) {
        case 1: cout << TFTP_ERROR_1;
            break;
        case 2: cout << TFTP_ERROR_2;
            break;
        case 3: cout << TFTP_ERROR_3;
            break;
        case 4: cout << TFTP_ERROR_4;
            break;
        case 5: cout << TFTP_ERROR_5;
            break;
        case 6: cout << TFTP_ERROR_6;
            break;
        case 7: cout << TFTP_ERROR_7;
            break;
        case 0:
        default: cout << TFTP_ERROR_0;
            break;
    }

    this->~TFTPClient();

}

template<class Caller_P> TFTPClient<Caller_P>::~TFTPClient() {

    if (socket_descriptor != -1) {
        disconnect();
    }

}

template<class Caller_P> void TFTPClient<Caller_P>::disconnect() {
#if defined _WIN32 || _WIN64

    closesocket(socket_descriptor);
    //WSACleanup();

#else

    close(socket_descriptor);

#endif
}
#endif
