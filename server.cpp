#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <queue>
#include <iostream>
#include <fstream>

#include "utils.h"

using namespace std;

struct ComparePairs {
    bool operator()(const pair<unsigned short, packet>& p1, const pair<unsigned short, packet>& p2) const {
        return p1.first > p2.first;
    }
};

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    socklen_t addr_size = sizeof(client_addr_from);
    unsigned short expected_seq_num = 1;
    int recv_len;
    struct packet ack_pkt;

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Configure the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the server address
    if (bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Configure the client address structure to which we will send ACKs
    memset(&client_addr_to, 0, sizeof(client_addr_to));
    client_addr_to.sin_family = AF_INET;
    client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST);
    client_addr_to.sin_port = htons(CLIENT_PORT_TO);

    // Open the target file for writing (always write to output.txt)
    FILE *fp = fopen("output.txt", "wb");

    bool last_packet = false;

    // Use priority queue to handle out-of-order packets
    priority_queue<pair<unsigned short, packet>, vector<pair<unsigned short, packet> >, ComparePairs> unordered;

    // Loop to receive and process packets
    while (1) {
        // Receive a packet from the client
        struct packet buffer;
        recv_len = recvfrom(listen_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr_from, &addr_size);
        // printRecv(&buffer);
        if (recv_len < 0) {
            perror("Receive failed");
            break;
        }

        unsigned short seq = buffer.seqnum;

        // In-order packet, write to file
        if (seq == expected_seq_num) {
            fwrite(buffer.payload, 1, buffer.length, fp);
            if (buffer.last == 1) last_packet = true;
            expected_seq_num += 1;

            // Process any consecutive in-order packets from the priority queue
            while (!unordered.empty() && unordered.top().first == expected_seq_num) {
                pair<unsigned short, packet> segment = unordered.top();
                unordered.pop();
                fwrite(segment.second.payload, 1, segment.second.length, fp);
                if (segment.second.last == 1) last_packet = true;
                expected_seq_num += 1;
            }
            
        } 

        // Out-of-order packet, push to the priority queue
        else if (seq > expected_seq_num) { 
            pair<unsigned short, packet> current = make_pair(buffer.seqnum, buffer);
            unordered.push(current);
        }

        // Check if all packets have been received 
        // Build an acknowledgment packet
        if (last_packet) build_packet(&ack_pkt, 0, expected_seq_num, 1, 1, 0, NULL);
        else build_packet(&ack_pkt, 0, expected_seq_num, 0, 1, 0, NULL);
        
        // Send the acknowledgment packet to the client
        sendto(send_sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client_addr_to, sizeof(client_addr_to));
        // printSend(&ack_pkt, 0);

        // Stop the server after finished the last packet
        if (last_packet) break;
    }
    

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}