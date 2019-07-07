#include <ifaddrs.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>

#include "p2pjs.h"

internal char* GetIPAddressString(const struct sockaddr*, char*, size_t);

internal int 
GetLocalIPAddress(char *s, size_t maxLen)
{
    struct ifaddrs *iflist = 0;
    if (getifaddrs(&iflist) == 0)
    {
        bool32 found = 0;
        for (struct ifaddrs *iface = iflist; iface != 0; iface = iface->ifa_next)
        {
            if (!iface->ifa_addr)
                continue;
            // NOTE(Kevin): Try to filter out loopback intrerfaces
            if (strcmp(iface->ifa_name, "en0") == 0 ||
                strcmp(iface->ifa_name, "eth0") == 0 ||
                strcmp(iface->ifa_name, "wlan0") == 0)
            {
                if (GetIPAddressString(iface->ifa_addr, s, maxLen))
                {
                    found = 1;
                    break;
                }
            }
        }
        freeifaddrs(iflist);
        if (found)
            return kSuccess;
    }
    return kNoAvailableInterface;
}
