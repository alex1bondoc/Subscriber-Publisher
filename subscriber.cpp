#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>

using namespace std;

#define MAX_MSG_SIZE 1600

int main(int argc, char *argv[]) {
    // pentru afisarea instantanee a mesajelor in consola
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    // verificam daca avem suficiente argumente
    if (argc != 4) {
        return 1;
    }
    // luam argumentele pentru id, server_ip si server_port
    char *id = argv[1];
    char *server_ip = argv[2];
    int port = atoi(argv[3]);

    // creem un fd pentru socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        // a aparut o eroare
        return 1;
    }
    // creem structura de sockaddr_in
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port); // legam structura de portul serverului in network order
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) { // copiem in adresa ip adresa ip a serverului
        // a aparut o eroare
        close(sockfd); // inchidem fd deschis mai devreme pentru socket
        return 1;
    }
    // cerem o conexiune pentru adresa si portul respectiv
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        // a aparut o eroare
        close(sockfd); // inchidem fd
        return 1;
    }
    // trimitem id-ul pentru conectare la server
    send(sockfd, id, strlen(id), 0);

    // lista pentru poll
    struct pollfd fds[2];
    fds[0] = {STDIN_FILENO, POLLIN, 0}; // input de la tastatura
    fds[1] = {sockfd, POLLIN, 0}; // input de pe socket

    // bufferul pentru a asigura ca afisam toate mesajele corect
    char recv_buffer[MAX_MSG_SIZE * 2];
    size_t recv_len = 0;

    while (true) {
        // poll
        int rc = poll(fds, 2, -1);
        if (rc < 0) {
            break;
        }
        // de la tastatura verifica daca e una din comenzile exit subscribe unsubscribe
        if (fds[0].revents & POLLIN) {
            char input[MAX_MSG_SIZE];
            cin.getline(input, sizeof(input)); // citim imputul de la tastatura
            
            if (strlen(input) == 0) continue; // input gol 
            if (strcmp(input, "exit") == 0) {
                // deconectam subscriberul de la server
                close(sockfd);
                break;
            }
            // luam topicul 
            char *space = strchr(input, ' ');
            char *topic = space + 1;
            // trimitem mesajul de tip subscribe * / s
            send(sockfd, input, strlen(input), 0);
            if (strncmp(input, "subscribe", 9) == 0) {
                cout << "Subscribed to topic " << topic << endl;
            } else if (strncmp(input, "unsubscribe", 11) == 0) {
                cout << "Unsubscribed from topic " << topic << endl;
            }
        }else if (fds[1].revents & POLLIN) {
            // daca primim mesaj de la server
            char buf[MAX_MSG_SIZE];
            // luam mesajul
            int rc = recv(sockfd, buf, sizeof(buf) - 1, 0);
            if (rc > 0) {
                // daca am luat macar un byte
                buf[rc] = '\0';
                // asiguram ca bufferul se termina cu final de string
                if (recv_len + rc < sizeof(recv_buffer)) {
                    // daca putem sa adaugam mesajul in bufferul de printare
                    memcpy(recv_buffer + recv_len, buf, rc);
                    recv_len += rc;
                    recv_buffer[recv_len] = '\0';

                    char *line_end;
                    // scriem mereu cand gasim cate un endline in buffer pentru a fi siguri ca printam numai mesajele intregi
                    while ((line_end = strchr(recv_buffer, '\n')) != nullptr) {
                        *line_end = '\0';
                        cout << recv_buffer << endl;
                        // actualizam bufferul de print dupa ce am scos din el mesajul printat
                        size_t remaining = recv_len - (line_end - recv_buffer + 1);
                        memmove(recv_buffer, line_end + 1, remaining);
                        recv_len = remaining;
                        recv_buffer[recv_len] = '\0';
                    }
                }
            } else if (rc == 0) {
                break;
            }
        }
    }
    return 0;
}