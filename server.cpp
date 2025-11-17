#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unordered_set>
#include <sstream>
#include <netinet/tcp.h>
#include <cmath>
using namespace std;

#define MAX_MSG_SIZE 1600
#define TOPIC_LEN 50
#define ID_LEN 10


struct Client {
    char id[ID_LEN + 1]; // id client
    int fd = -1; // fd -1 inseamna ca nu e conectat la
    unordered_set<string> subs; // lista de subscriptii
};


// functie care calculeaza daca un pattern se potriveste unui topic
// explicat putin si in readme
bool topic_matches(const char* pattern, const char* topic) {
    // extragem din pattern si topic partile care sunt separate de /
    char pat_copy[51];
    char top_copy[51];
    strncpy(pat_copy, pattern, 50);
    strncpy(top_copy, topic, 50);
    pat_copy[50] = top_copy[50] = '\0';

    vector<const char*> pattern_parts;
    char* pat_part = strtok(pat_copy, "/");
    while (pat_part) {
        pattern_parts.push_back(pat_part);
        pat_part = strtok(NULL, "/");
    }

    vector<const char*> topic_parts;
    char* top_part = strtok(top_copy, "/");
    while (top_part) {
        topic_parts.push_back(top_part);
        top_part = strtok(NULL, "/");
    }
    // numarul de cuvinte din pattern si din topic
    size_t n = pattern_parts.size();
    size_t m = topic_parts.size();
    // rezolvare cu programare dinamica pentru a vedea daca se potriveste patternul 
    vector<vector<bool>> dp(n + 1, vector<bool>(m + 1, false));
    dp[0][0] = true;
    for (size_t i = 1; i <= n; ++i) {
        const char* p = pattern_parts[i-1];
        for (size_t j = 1; j <= m; ++j) {
            const char* t = topic_parts[j-1];
            if (strcmp(p, "+") == 0) {
                dp[i][j] = dp[i-1][j-1];
            } else if (strcmp(p, "*") == 0) {
                dp[i][j] = dp[i-1][j-1] || dp[i][j-1];
            } else {
                dp[i][j] = (strcmp(p, t) == 0) && dp[i-1][j-1];
            }
        }
    }
    // se potriveste sau nu patternul
    return dp[n][m];
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    vector<Client> clients; // lista clientilor de la server
    clients.push_back({"", -1}); // client null ca indexul clientilor sa inceapa de la 1
    unordered_map<int, size_t> fd_to_client; // un map pentru a obtine mai usor un index al clientului dupa fd
    unordered_map<string, size_t> id_to_index; // un map pentru a obtine indexul mai usor dupa numele clientului
    // verificam daca am primit port ca si argument
    if (argc != 2) {
        return 1;
    }
    // obtinem portul din argumente
    int port = atoi(argv[1]);
    int tcpfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpfd < 0) {
        return 1;
    }
    int udpfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpfd < 0) {
        close(tcpfd);
        return 1;
    }

    int enable = 1;
    setsockopt(tcpfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    setsockopt(udpfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    // dezactivam algoritmul lui Nagle
    setsockopt(tcpfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int));
    struct sockaddr_in serv_addr{};
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(tcpfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(tcpfd);
        close(udpfd);
        return 1;
    }

    if (bind(udpfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(tcpfd);
        close(udpfd);
        return 1;
    }
    if (listen(tcpfd, SOMAXCONN) < 0) {
        close(tcpfd);
        close(udpfd);
        return 1;
    }
    // creem o lista dinamica de pentru poll in care punem initial
    // stdin pentru input tastatura
    // udpfd pentru mesaje de la clienti udp
    // tcpfd pentru noi conexiuni
    vector<pollfd> fds;
    fds.push_back({STDIN_FILENO, POLLIN, 0});
    fds.push_back({udpfd, POLLIN, 0});
    fds.push_back({tcpfd, POLLIN, 0});

    while (true) {
        // folosim poll pentru a astepta mesaje -1 pentru Timeout
        int rc = poll(fds.data(), fds.size(), -1);
        if (rc < 0) {
            break;
        }
        // verificam daca primim mesaj de la vreun fd
        for (size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].revents & POLLIN) {
                // daca primim mesaj verificam ce fel de mesaj este
                // daca este de la tastatura
                if (fds[i].fd == STDIN_FILENO) {
                    char buf[MAX_MSG_SIZE];
                    cin.getline(buf, MAX_MSG_SIZE);
                    // vedem daca este un mesaj de exit
                    if (strcmp(buf, "exit") == 0) {
                        // daca da atunci vedem care sunt clientii conectati si ii deconectam de la server
                        for (int j=0;j<clients.size();++j) {
                            if (clients[j].fd != -1) close(clients[j].fd);
                        }
                        // inchidem fd-urile si oprim aplicatia
                        close(tcpfd);
                        close(udpfd);
                        return 0;
                    }
                }
                else if (fds[i].fd == udpfd) {
                    // daca este de tip udp vom folosi recvfrom pentru a lua mesajul si adresa si portul clientului de la care primim mesajul
                    struct sockaddr_in cli_addr{};
                    socklen_t len = sizeof(cli_addr);
                    char buf[MAX_MSG_SIZE]{};
                    // salvam in cli_addr adresa si portul clientului
                    int rc = recvfrom(udpfd, buf, sizeof(buf), 0, 
                                    (struct sockaddr*)&cli_addr, &len);
                    // daca nu am primit nimic trecem peste
                    if (rc < 0) {
                        continue;
                    }   
                    // extragem topicul din mesaj 
                    char topic[TOPIC_LEN+1] = {0};
                    memcpy(topic, buf, TOPIC_LEN);
                    // extragem char-ul pnetru type
                    uint8_t type = buf[TOPIC_LEN];
                    char value[MAX_MSG_SIZE] ={0};
                    char type_str[20] = {0};
                    // luam ip si port de la client
                    const char* ip = inet_ntoa(cli_addr.sin_addr);
                    uint16_t port = ntohs(cli_addr.sin_port);
                    // creem mesajul in functie de type
                    switch (type) {
                        case 0: { // INT
                            // primul char este de semn iar restul creeaza numarul
                            uint8_t sign = buf[TOPIC_LEN + 1];
                            // creem valoarea
                            uint32_t val = ntohl(*(uint32_t*)(buf + TOPIC_LEN + 2));
                            // verificam ce semn trebuie sa aibe
                            if (sign == 1)
                                val = -val;
                            // creem partea din mesaj necesara
                            snprintf(value, sizeof(value), "%d", val);
                            strcpy(type_str, "INT");
                            break;
                        }
                        case 1: { // SHORT_REAL
                            // obtinem numarul
                            uint16_t raw = ntohs(*(uint16_t*)(buf + TOPIC_LEN + 1));
                            // creem mesajul cum ne este cerut
                            snprintf(value, sizeof(value), "%.2f", raw / 100.0);
                            strcpy(type_str, "SHORT_REAL");
                            break;
                        }
                        case 2: { // FLOAT
                            // luam primul char pentru semn
                            uint8_t sign = buf[TOPIC_LEN + 1];
                            // restul de octeti pentru numar
                            uint32_t val = ntohl(*(uint32_t*)(buf + TOPIC_LEN + 2));
                            // urmatorul char pentru puterea lui 10
                            uint8_t power = buf[TOPIC_LEN + 6];
                            // calculam numarul
                            double res = val / pow(10, power);
                            // verificam semnul
                            if(sign == 1)
                                res *= -1;
                            // creem mesajul
                            snprintf(value, sizeof(value), "%.*f", power, res);
                            strcpy(type_str, "FLOAT");
                            break;
                        }
                        case 3: // STRING
                            // creem mesajul
                            strcpy(type_str, "STRING");
                            strncpy(value, buf + TOPIC_LEN + 1, rc - (TOPIC_LEN + 1));
                            value[rc - (TOPIC_LEN + 1)] = '\0';
                            break;
                    }
                    // creem forma finala a mesajului
                    char msg[MAX_MSG_SIZE]{};
                    snprintf(msg, sizeof(msg), "%s:%hu - %s - %s - %s\n",
                            ip, port, topic, type_str, value);
                    // luam fiecare client in parte
                    for (size_t j = 0; j < clients.size(); j++) {
                        Client& c = clients[j];
                        // verificam daca este activ
                        if (c.fd == -1) continue;

                        for (auto& pattern : c.subs) {
                            // cautam in lista de subscriptii topicul
                            if (topic_matches(pattern.c_str(), topic)) {
                                size_t msg_len = strlen(msg);
                                const char* msg_ptr = msg;
                                while (msg_len > 0) {
                                    // trimitem mesajul pana cand nu mai ramane nimic din mesaj
                                    int sent = send(c.fd, msg_ptr, msg_len, 0);
                                    if (sent <= 0) {
                                        if (errno == EINTR) continue;
                                        break;
                                    }
                                    msg_ptr += sent;
                                    msg_len -= sent;
                                }
                                break;
                            }
                        }
                    }
                } else if (fds[i].fd == tcpfd) {
                    // daca avem o noua conexiune tcp
                    struct sockaddr_in cli_addr{};
                    socklen_t len = sizeof(cli_addr);
                    // acceptam
                    int newsock = accept(tcpfd, (struct sockaddr*)&cli_addr, &len);
                    if (newsock < 0) {
                        continue;
                    }
                    // luam id-ul primit 
                    char id[ID_LEN + 1] = {0};
                    int rc = recv(newsock, id, ID_LEN, 0);
                    if (rc <= 0) {
                        close(newsock);
                        continue;
                    }
                    id[rc] = '\0';

                    string id_str(id);
                    int index = id_to_index[id_str];
                    // vedem daca clientul exista deja in lista
                    if (index > 0) {
                        // verificam daca este deja conectat
                        if (clients[index].fd != -1) {
                            cout << "Client " << id << " already connected.\n";
                            close(newsock);
                            continue;
                        }
                        // daca nu e ii schimbam fd-ul 
                        clients[index].fd = newsock;
                        fd_to_client[newsock] = index;
                    } else {
                        // daca nu creem un client nou de adaugat la vector
                        Client new_client;
                        strncpy(new_client.id, id, ID_LEN);
                        new_client.id[ID_LEN] = '\0';
                        new_client.fd = newsock;
                        clients.push_back(new_client);
                        int last = clients.size() - 1;
                        id_to_index[id_str] = last;
                        fd_to_client[newsock] = last;
                    }
                    // adaugam fd-ul la lista de poll pentru a astepta de la el mesaje
                    fds.push_back({newsock, POLLIN, 0});
                    cout << "New client " << id << " connected from "
                        << inet_ntoa(cli_addr.sin_addr) << ":"
                        << ntohs(cli_addr.sin_port) << endl;
                } 
                else {
                    // mesaje de la subscriberi
                    int fd = fds[i].fd;
                    char buf[MAX_MSG_SIZE] = {0};
                    int rc = recv(fd, buf, sizeof(buf), 0);
                    if (rc <= 0) {
                        // daca nu am primit nimic de la client
                        // inseamna ca clientul s-a deconectat
                        if (fd_to_client.count(fd)) {
                            size_t ci = fd_to_client[fd];
                            cout << "Client " << clients[ci].id << " disconnected.\n";
                            clients[ci].fd = -1;
                            fd_to_client.erase(fd);
                        }
                        close(fd);
                        // il scoatem din mapul de fd to index
                        fds.erase(fds.begin() + i);
                        i--;
                    } else {
                        // daca nu atunci vedem ce mesaj a trimis pentru subscribe sau unsubscribe
                        int index = fd_to_client[fd];
                        Client& c = clients[index];
                        // impartim mesajul in comanda si topic
                        char* token = strtok(buf, " \n");
                        while (token) {
                            if (strcmp(token, "subscribe") == 0) {
                                char* topic = strtok(NULL, " \n");
                                if (topic) {
                                    string topic_str(topic);
                                    // creem string pentru a putea fi mai usor de adaugat in wildcard_list sau lista de topicuri
                                    c.subs.insert(topic_str);
                                }
                            } else if (strcmp(token, "unsubscribe") == 0) {
                                // aici doar elimininam topicul din lista mapuri
                                char* topic = strtok(NULL, " \n");
                                if (topic) {
                                    string topic_str(topic);
                                    c.subs.erase(topic_str);
                                }
                            }
                            token = strtok(NULL, " \n");
                        }
                    }
                }
            }
        }
    }

    close(tcpfd);
    close(udpfd);
    return 0;
}