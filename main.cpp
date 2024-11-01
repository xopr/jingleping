#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/icmp6.h>
#include <unistd.h>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define ICMPV6_ECHO_REQUEST 128
#define ICMPV6_ECHO_REPLY 129

// #define DEBUG
// #define SIMULATE


struct icmp6_hdr create_icmp6_echo_request(uint16_t id, uint16_t seq);
uint16_t checksum(void *b, int len);

int main(int argc, char *argv[]) {
    int width, height, channels;

    uint8_t* img = stbi_load("image.png", &width, &height, &channels, 4);

    if (img == NULL)
    {
        std::cerr << "Failed to load image.png" << std::endl;
        return 1;
    }

    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <IPv6 address> <xOffset> <yOffset>" << std::endl;
        return 1;
    }

    const uint16_t xOffset = std::stoi(argv[2]); // was: 600
    const uint16_t yOffset = std::stoi(argv[3]); // was: 350

    if ( xOffset + width > 1920 )
    {
        std::cerr << "Target x too wide, max " << (1920 - width) << std::endl;
        return 1;
    }
    if ( yOffset + height > 1080 )
    {
        std::cerr << "Target y too high, max " << (1080 - height) << std::endl;
        return 1;
    }

    // Resolve target IPv6 address
    struct sockaddr_in6 target_addr{};
    target_addr.sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, argv[1], &target_addr.sin6_addr) != 1) {
        std::cerr << "Invalid IPv6 address format" << std::endl;
        return 1;
    }

    std::cout << "W:" << width
              << ", H:" << height
            //   << " CH:" << channels
              << " @ " << xOffset
              << ", " << yOffset
              << std::endl;

#ifndef SIMULATE
    // Set timeout for socket receive
    struct timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 1;

    int socks[width] = {};

    for(uint16_t x = 0; x < width; ++x)
    {
        socks[x] = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
        if (socks[x] < 0) {
            perror("socket creation failed");
            return 1;
        }

        setsockopt(socks[x], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }
    // Create raw socket for ICMPv6
    // int sockfd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    // if (sockfd < 0) {
    //     perror("socket creation failed");
    //     return 1;
    // }


    // Create ICMPv6 echo request
    uint16_t id = getpid() & 0xFFFF;
    uint16_t seq = 1;
    auto icmp_req = create_icmp6_echo_request(id, seq);
#endif

    // Max 1920x1080
    // 2001:610:1908:a000:<X>:<Y>:<B><G>:<R><A>
    // 2001:610:1908:a000:0019:0019:00d1:ffff -> SNT Yellow #FFD100 at 25, 25
    char ip6str[INET6_ADDRSTRLEN];
    uint8_t* p = img;
    char symbol = ' ';



    for(uint16_t y = 0; y < height; ++y)
    {
        const uint16_t yy = yOffset + y;

        // Note that IPv6 has the wrong endian to assign uint16_t
        target_addr.sin6_addr.__in6_u.__u6_addr8[11] = yy & 0xff; // YL
        target_addr.sin6_addr.__in6_u.__u6_addr8[10] = yy >> 8; // YH
            // target_addr.sin6_addr.__in6_u.__u6_addr8[0] = 1;

        for(uint16_t x = 0; x < width; ++x)
        {
            const uint16_t xx = xOffset + x;

            target_addr.sin6_addr.__in6_u.__u6_addr8[9] = xx & 0xff; // XL
            target_addr.sin6_addr.__in6_u.__u6_addr8[8] = xx >> 8; // XH

            target_addr.sin6_addr.__in6_u.__u6_addr8[14] = *p;     // R


#ifdef DEBUG
            // .oO0
            switch (*p >> 6)
            {
                case 0: symbol = '.'; break;
                case 1: symbol = 'o'; break;
                case 2: symbol = 'O'; break;
                case 3: symbol = '0'; break;
            }
            
#endif

            target_addr.sin6_addr.__in6_u.__u6_addr8[13] = *(++p); // G (+inc)
            target_addr.sin6_addr.__in6_u.__u6_addr8[12] = *(++p); // B (+inc)
            target_addr.sin6_addr.__in6_u.__u6_addr8[15] = *(++p); // A (+inc)

            // If no alpha, don't bother sending a ping for it (increase afterwards)
            // if (!*p++)
            if (!*p++)
            {
#ifdef DEBUG
                // std::cout << "transparent pixel " << xx << ", " << yy << std::endl;
                std::cout << " ";
#endif
                continue;
            }

            // ++p;
            // p += channels;

            // target_addr.sin6_addr.__in6_u.__u6_addr8[0] = 1;

            // struct sockaddr_in sa;
            inet_ntop(AF_INET6, &(target_addr.sin6_addr), ip6str, INET6_ADDRSTRLEN);


#ifndef SIMULATE
            // Send ICMPv6 echo request
            // auto start = std::chrono::high_resolution_clock::now();
            ssize_t sent_bytes = sendto(socks[x], &icmp_req, sizeof(icmp_req), 0,
                                        (struct sockaddr *)&target_addr, sizeof(target_addr));
            if (sent_bytes <= 0) {
                perror("Failed to send ICMPv6 echo request");
                // close(sockfd);
                // return 1;
            }

            // inet_ntop(AF_INET6, &(target_addr.sin6_addr), ip6str, INET6_ADDRSTRLEN);
            // std::cout << "ICMPv6 Echo Request sent to " << ip6str << std::endl;
            // std::cout << ".";

            // Receive ICMPv6 echo reply
            char buffer[128];
            struct sockaddr_in6 reply_addr{};
            socklen_t addr_len = sizeof(reply_addr);
            ssize_t received_bytes = recvfrom(socks[x], buffer, sizeof(buffer), 0,
                                            (struct sockaddr *)&reply_addr, &addr_len);
            if (received_bytes <= 0) {
                // std::cerr << "No reply received (timeout or error)" << std::endl;
            } else {
                // auto end = std::chrono::high_resolution_clock::now();
                // std::chrono::duration<double, std::milli> elapsed = end - start;
                // std::cout << "ICMPv6 Echo Reply received from " << argv[1] 
                //           << " in " << elapsed.count() << " ms" << std::endl;
            }


            // close(sockfd);
#endif




#ifdef DEBUG
            // std::cout << "ICMPv6 Echo Request sent to " << ip6str << std::endl;
            std::cout << symbol;
#endif

        }
#ifdef DEBUG
        std::cout << std::endl;
#endif
    }

    stbi_image_free(img);


#ifndef SIMULATE
    // // Send ICMPv6 echo request
    // auto start = std::chrono::high_resolution_clock::now();
    // ssize_t sent_bytes = sendto(sockfd, &icmp_req, sizeof(icmp_req), 0,
    //                             (struct sockaddr *)&target_addr, sizeof(target_addr));
    // if (sent_bytes <= 0) {
    //     perror("Failed to send ICMPv6 echo request");
    //     // close(sockfd);
    //     // return 1;
    // }

    // inet_ntop(AF_INET6, &(target_addr.sin6_addr), ip6str, INET6_ADDRSTRLEN);

    // std::cout << "ICMPv6 Echo Request sent to " << ip6str << std::endl;

    // // Receive ICMPv6 echo reply
    // char buffer[128];
    // struct sockaddr_in6 reply_addr{};
    // socklen_t addr_len = sizeof(reply_addr);
    // ssize_t received_bytes = recvfrom(sockfd, buffer, sizeof(buffer), 0,
    //                                   (struct sockaddr *)&reply_addr, &addr_len);
    // if (received_bytes <= 0) {
    //     std::cerr << "No reply received (timeout or error)" << std::endl;
    // } else {
    //     auto end = std::chrono::high_resolution_clock::now();
    //     std::chrono::duration<double, std::milli> elapsed = end - start;
    //     std::cout << "ICMPv6 Echo Reply received from " << argv[1] 
    //               << " in " << elapsed.count() << " ms" << std::endl;
    // }


    // close(sockfd);
    for(uint16_t x = 0; x < width; ++x)
    {
        close(socks[x]);
    }

#endif

    return 0;
}

// Function to create ICMPv6 echo request
struct icmp6_hdr create_icmp6_echo_request(uint16_t id, uint16_t seq) {
    struct icmp6_hdr icmp6_req{};
    icmp6_req.icmp6_type = ICMPV6_ECHO_REQUEST;
    icmp6_req.icmp6_code = 0;
    icmp6_req.icmp6_id = htons(id);
    icmp6_req.icmp6_seq = htons(seq);
    icmp6_req.icmp6_cksum = 0;  // Set to 0 before checksum calculation
    icmp6_req.icmp6_cksum = checksum(&icmp6_req, sizeof(icmp6_req));
    return icmp6_req;
}

// Function to calculate checksum
uint16_t checksum(void *b, int len) {
    uint16_t *buf = reinterpret_cast<uint16_t*>(b);
    uint32_t sum = 0;
    uint16_t result;

    for (sum = 0; len > 1; len -= 2) {
        sum += *buf++;
    }
    if (len == 1) {
        sum += *(uint8_t*)buf;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}
