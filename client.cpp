#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fstream>
#include <cmath>
#include <iostream>

#include "utils.h"

using namespace std;

void transmit_with_seq_num(ifstream &inputFile, unsigned short seq_num, int lastSeqNum,
                           int &send_sockfd, int &listen_sockfd,
                           struct sockaddr_in &server_addr_to,
                           socklen_t &addr_size, struct timeval &tv);

int main(int argc, char *argv[]) {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);
    struct timeval tv;
    unsigned short seq_num = 1;
    unsigned short prev_ack = 0;
    int dup_cnt = 0;
    int ssh = 40;
    int cwnd = 1;
    int windowLeft = 1;
    int windowRight = 1;
    int windowEnd = 1;
    Phase algoPhase = slow_start;

    // read filename from command line argument
    if (argc != 2) {
        printf("Usage: ./client <filename>\n");
        return 1;
    }
    char *filename = argv[1];

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Configure the server address structure to which we will send data
    memset(&server_addr_to, 0, sizeof(server_addr_to));
    server_addr_to.sin_family = AF_INET;
    server_addr_to.sin_port = htons(SERVER_PORT_TO);
    server_addr_to.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Configure the client address structure
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_PORT);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the client address
    if (bind(listen_sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // set listen socket timeout to 1s
    tv.tv_sec = 0;
    tv.tv_usec = 900000;

    // Open file for reading
    ifstream inputFile;
    inputFile.open(filename);
    if (!inputFile.is_open()) {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }
    inputFile.seekg(0, inputFile.end);
    streampos fileSize = inputFile.tellg();
    inputFile.seekg(0, ios::beg);
    int lastSeqNum = ceil((double)fileSize / PAYLOAD_SIZE);

    // Read from file, and initiate reliable data transfer to the server
    transmit_with_seq_num(inputFile, seq_num, lastSeqNum, send_sockfd,
                          listen_sockfd, server_addr_to, addr_size, tv);

    // ACK listening
    while (true) {
        struct packet ack_pkt;
        ssize_t bytesReceived =
            recvfrom(listen_sockfd, (char *)&ack_pkt, sizeof(ack_pkt), 0,
                     (struct sockaddr *)&server_addr_from, &addr_size);

        // Received ack packet within timeout
        if (bytesReceived > 0) {
            // Duplicated ack received
            if (ack_pkt.acknum == prev_ack) {
                dup_cnt++;
            } 
            // New ack received, reset duplicate count
            else {
                if (algoPhase == fast_retransmit) {
                    cwnd = ssh+1;
                    algoPhase = congestion_avoidance;
                    windowEnd = ack_pkt.acknum + cwnd - 1;
                }
                dup_cnt = 0;
                prev_ack = ack_pkt.acknum;
            }

            // Start fast retransmit
            if (dup_cnt == 3) {  
                algoPhase = fast_retransmit;
                // Retransmit packet with seq_num = ack_pkt.ack
                transmit_with_seq_num(inputFile, ack_pkt.acknum, lastSeqNum,
                                      send_sockfd, listen_sockfd,
                                      server_addr_to, addr_size, tv);
                ssh = max(2, cwnd / 2);
                cwnd = ssh + 3;
                windowEnd = ack_pkt.acknum + cwnd - 1;
            } else if (dup_cnt > 3) {
                cwnd += 1;
                windowEnd += 1;
            }

            // Switch to congestion avoidance when cwnd > ssh
            if (algoPhase == slow_start && cwnd > ssh) {
                algoPhase = congestion_avoidance;
            }

            // Pop the window received until the current ACK number
            while (windowLeft <= windowRight && windowLeft < ack_pkt.acknum) {
                windowLeft += 1;
            }

            // Slow start cwnd update mechanism
            if (algoPhase == slow_start) {
                cwnd += 1;
                windowEnd = ack_pkt.acknum + cwnd - 1;
            }

            // Congestion control cwnd update mechanism
            else if (algoPhase == congestion_avoidance && ack_pkt.acknum > windowEnd) {
                cwnd += 1;
                windowEnd += cwnd;
            }

            // Transmit the packet until window is full
            while ((windowRight - windowLeft + 1) < cwnd &&
                   windowRight < lastSeqNum) {  
                windowRight += 1;
                seq_num = windowRight + 1;
                transmit_with_seq_num(inputFile, seq_num, lastSeqNum,
                                      send_sockfd, listen_sockfd,
                                      server_addr_to, addr_size, tv);
                // window.push_back(seq_num);
                
            }
        } 
        
        // Time out event
        else {  
            // Update variables
            ssh = max(cwnd / 2, 2);
            cwnd = 1;
            prev_ack = 0;
            windowEnd = windowLeft;
            algoPhase = slow_start;
            // Retransmit first unackownledged packet
            seq_num = windowEnd;
            transmit_with_seq_num(inputFile, seq_num, lastSeqNum,
                                  send_sockfd, listen_sockfd, server_addr_to,
                                  addr_size, tv);
        }
    }

    inputFile.close();
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

void transmit_with_seq_num(ifstream &inputFile, unsigned short seq_num, int lastSeqNum,
                           int &send_sockfd, int &listen_sockfd,
                           struct sockaddr_in &server_addr_to,
                           socklen_t &addr_size, struct timeval &tv) {
    struct packet pkt;
    char buffer[PAYLOAD_SIZE];
    inputFile.clear();
    inputFile.seekg((seq_num - 1) * PAYLOAD_SIZE, ios_base::beg); // move ptr to retransmitting payload
    inputFile.read(buffer, sizeof(buffer));  // read retransmitting packet
    streamsize bytesRead = inputFile.gcount();
    char last = seq_num == lastSeqNum ? 1 : 0;
    build_packet(&pkt, seq_num, 0, last, 0, bytesRead, buffer);
    // Send new packet, reset timeout
    while (sendto(send_sockfd, (const char *)&pkt, sizeof(pkt), 0,
                  (struct sockaddr *)&server_addr_to, addr_size) < 0) {    }
    setsockopt(listen_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
}