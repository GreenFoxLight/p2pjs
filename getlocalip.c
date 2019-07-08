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
        // NOTE(Kevin): Prefer ipv4 addresses
        bool32 found = 0;
        for (struct ifaddrs *iface = iflist; iface != 0; iface = iface->ifa_next)
        {
            // NOTE(Kevin): Try to filter out loopback interfaces
            if (!iface->ifa_addr)
                continue;
            if (iface->ifa_addr->sa_family == AF_INET)
            {
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
        }
        if (found)
            return kSuccess;

        for (struct ifaddrs *iface = iflist; iface != 0; iface = iface->ifa_next)
        {
            // NOTE(Kevin): Try to filter out loopback interfaces
            if (!iface->ifa_addr)
                continue;
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
    }
    return kNoAvailableInterface;
}
