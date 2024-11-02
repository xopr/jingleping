#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/icmp6.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define PROGRESS // Show progress dots per image
#define LOOPS 1000 // Amount of times to do a full image
// Canvas sizes
#define WIDTH 1920
#define HEIGHT 1080

// Function to calculate checksum
uint16_t checksum(void *b, int len)
{
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

int main(int argc, char *argv[])
{
    // Check arguments
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <eth device> <IPv6 address> <xOffset> <yOffset>" << std::endl;
        return 1;
    }

    // Load image
    int width, height, channels;
    uint8_t* img = stbi_load("image.png", &width, &height, &channels, 4);
    if (img == NULL)
    {
        std::cerr << "Failed to load image.png" << std::endl;
        return 1;
    }

    // Determine offset
    const uint16_t xOffset = std::stoi(argv[3]); // was: 600
    const uint16_t yOffset = std::stoi(argv[4]); // was: 350

    if ( xOffset + width > WIDTH )
    {
        std::cerr << "Target x too wide, max " << (WIDTH - width) << std::endl;
        return 1;
    }
    if ( yOffset + height > HEIGHT )
    {
        std::cerr << "Target y too high, max " << (HEIGHT - height) << std::endl;
        return 1;
    }

    // Determine IP range
    struct sockaddr_in6 target_addr {};
    target_addr.sin6_family = AF_INET6;

    if (inet_pton(AF_INET6, argv[2], &target_addr.sin6_addr) != 1)
    {
        std::cerr << "Invalid IPv6 address format" << std::endl;
        return 1;
    }

    std::cout << "W:" << width
              << ", H:" << height
              << " @ " << xOffset
              << ", " << yOffset
              << std::endl;


    uint16_t id = getpid() & 0xFFFF;

    int socks[width] = {};
    struct icmp6_hdr icmp6;
    struct icmp6_filter filterv6;

    ICMP6_FILTER_SETBLOCKALL(&filterv6);
    ICMP6_FILTER_SETPASS(ICMP6_DST_UNREACH, &filterv6);
    ICMP6_FILTER_SETPASS(ICMP6_PACKET_TOO_BIG, &filterv6);
    ICMP6_FILTER_SETPASS(ICMP6_TIME_EXCEEDED, &filterv6);
    ICMP6_FILTER_SETPASS(ICMP6_PARAM_PROB, &filterv6);  
    ICMP6_FILTER_SETPASS(ICMP6_ECHO_REPLY, &filterv6);
    ICMP6_FILTER_SETPASS(ND_REDIRECT, &filterv6);

    icmp6.icmp6_type = ICMP6_ECHO_REQUEST;
    icmp6.icmp6_code = 0;
    icmp6.icmp6_id = htons(id);
    icmp6.icmp6_seq = htons(0);
    icmp6.icmp6_cksum = 0;
    // Note: checksum is optional but there seems not much overhead
    icmp6.icmp6_cksum = checksum(&icmp6, sizeof(icmp6));

    // Ping loop
    for(uint16_t loop = 0; loop < LOOPS; loop++)
    {
        // Update sequence
        icmp6.icmp6_seq = htons(loop);

#ifdef PROGRESS
        std::cout << ".";
        std::flush(std::cout);
#endif
        // Set image pointer
        uint8_t* p = img;

        for(uint16_t x = 0; x < width; ++x)
        {
            socks[x] = socket( AF_INET6, SOCK_RAW, IPPROTO_ICMPV6 );
            if (socks[x] < 0)
            {
                std::cerr << "socket creation failed" << std::endl;
                return 1;
            }

            if (setsockopt(socks[x], IPPROTO_ICMPV6, ICMP6_FILTER, &filterv6, sizeof(filterv6)))
            {
                std::cerr << "could not set filter params" << std::endl;
                return 1;
            }

            if (setsockopt(socks[x], SOL_SOCKET, SO_BINDTODEVICE, argv[1], strnlen(argv[1], 32)))
            {
                std::cerr << "could not bind to device" << std::endl;
                return 1;
            }

            // TODO: experiment with reuse address and port
            // if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
            //     error("setsockopt(SO_REUSEADDR) failed");            

        }

        for(uint16_t y = 0; y < height; ++y)
        {
            const uint16_t yy = yOffset + y;

            // Note that IPv6 has the wrong endian to assign uint16_t (needs htons or per-byte allocation)
            target_addr.sin6_addr.__in6_u.__u6_addr8[11] = yy & 0xff; // YL
            target_addr.sin6_addr.__in6_u.__u6_addr8[10] = yy >> 8; // YH

            for(uint16_t x = 0; x < width; ++x)
            {
                const uint16_t xx = xOffset + x;

                target_addr.sin6_addr.__in6_u.__u6_addr8[9] = xx & 0xff; // XL
                target_addr.sin6_addr.__in6_u.__u6_addr8[8] = xx >> 8; // XH

                target_addr.sin6_addr.__in6_u.__u6_addr8[14] = *p;     // R

                target_addr.sin6_addr.__in6_u.__u6_addr8[13] = *(++p); // G (+inc)
                target_addr.sin6_addr.__in6_u.__u6_addr8[12] = *(++p); // B (+inc)
                target_addr.sin6_addr.__in6_u.__u6_addr8[15] = *(++p); // A (+inc)

                // If no alpha, don't bother sending a ping for it (increase afterwards)
                if (!*p++)
                    continue;

                // Draw pinxel
                ssize_t result = sendto(socks[x], &icmp6, sizeof(icmp6), 0, (struct sockaddr *)&target_addr, sizeof(target_addr));
                if(result != sizeof(struct icmp6_hdr))
                {
                    std::cerr << "Failed to transmit" << std::endl;
                }
            }
        }

        for(uint16_t x = 0; x < width; ++x)
            close(socks[x]);
    }
}