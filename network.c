#include "network.h"

/*
Function to get IP adress of the interface "eth0"
Source: https://www.geekpage.jp/en/programming/linux-network/get-ipaddr.php
*/
void getIP(char *buffer) {
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);
    strcpy(buffer, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
}

/*
Function to get Mac address of the interface "eth0"
Source: https://community.onion.io/topic/2441/obtain-the-mac-address-in-c-code/2
*/
void getMac(char *buffer) {
    #define HWADDR_len 6

    int fd, i;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    strcpy(ifr.ifr_name, "eth0");
    ioctl(fd, SIOCGIFHWADDR, &ifr);
    close(fd);
    for (i = 0; i < HWADDR_len; i++)
        sprintf(&buffer[i*3], i == HWADDR_len-1 ? "%02X" : "%02X:", ((unsigned char*)ifr.ifr_hwaddr.sa_data)[i]);
    buffer[17]='\0';
}
