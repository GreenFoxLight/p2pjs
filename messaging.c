#include "p2pjs.h"
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>

internal int
SendBytes(int fd, int byteCount, const char *buffer)
{
    int outstanding = byteCount;
    while (outstanding > 0)
    {
        int did = send(fd, buffer, outstanding, 0);
        if (did == -1)
            return kSyscallFailed;
        outstanding -= did;
        buffer += did;
    }
    return kSuccess;
}

internal int
SendHello(int fd, const char *port)
{
    uint16 messageType = kHello;
    if (strlen(port) >= PeerPortLen)
        return kInvalidValue;
    char portBuffer[PeerPortLen];
    memset(portBuffer, 0, SizeofArray(portBuffer));
    strcpy(portBuffer, port);
    if (SendBytes(fd, sizeof(messageType), (const char*)&messageType) != kSuccess)
        return kSyscallFailed;
    if (SendBytes(fd, SizeofArray(portBuffer), portBuffer) != kSuccess)
        return kSyscallFailed;
    return kSuccess;
}

internal int
SendPeerList(int fd, uint16 numberOfPeers, const peer_info *peers)
{
    uint16 messageType = kPeerList;
    if (SendBytes(fd, sizeof(messageType), (const char*)&messageType) != kSuccess)
        return kSyscallFailed;
    if (SendBytes(fd, sizeof(numberOfPeers), (const char*)&numberOfPeers) != kSuccess)
        return kSyscallFailed;
    if (numberOfPeers > 0)
    {
        return SendBytes(fd, sizeof(peer_info) * numberOfPeers, (const char*)peers);
    }
    return kSuccess;
} 

internal int
SendGetPeers(int fd)
{
    uint16 messageType = kGetPeers;
    return SendBytes(fd, sizeof(messageType), (const char*)&messageType);
}

internal int 
ReceiveBytes(int fd, int byteCount, char *buffer)
{
    int outstanding = byteCount;
    while (outstanding > 0)
    {
        // TODO(Kevin): Figure out flags
        int did = recv(fd, buffer, outstanding, 0);
        if (did == -1)
        {
            return kSyscallFailed;
        }
        outstanding -= did;
        buffer += did;
    }
    return kSuccess;
}

internal int 
ReceiveMessage(int fd, message **messageOut)
{
    uint16 messageType;
    if (ReceiveBytes(fd, sizeof(uint16), (char*)&messageType) != kSuccess)
    {
        return kSyscallFailed;
    }
    switch (messageType)
    {
        case kHello: {
            char port[PeerPortLen];
            if (ReceiveBytes(fd, SizeofArray(port), &port[0]) != kSuccess)
            {
                return kSyscallFailed;
            }
            message *msg = malloc(sizeof(message));
            if (!msg)
                return kNoMemory;
            msg->type = kHello;
            memcpy(msg->hello.port, port, SizeofArray(port));
            *messageOut = msg; 
        } break;
        case kGetPeers: {
            // NOTE(Kevin): Get peers has no data attached
            message *msg = malloc(sizeof(message));
            if (!msg)
                return kNoMemory;
            msg->type = kGetPeers;
            *messageOut = msg;
        } break;
        case kPeerList: {
            uint16 numberOfPeers;
            if (ReceiveBytes(fd, sizeof(uint16), (char*)&numberOfPeers) != kSuccess)
            {
                return kSyscallFailed;
            }
            peer_info *peers = 0;
            if (numberOfPeers > 0)
            {
                peers = malloc(sizeof(peer_info) * numberOfPeers);
                if (!peers)
                    return kNoMemory;
                for (uint16 i = 0; i < numberOfPeers; ++i)
                {
                    if (ReceiveBytes(fd, sizeof(peer_info), (char*)&peers[i]) != kSuccess)
                    {
                        free(peers);
                        return kSyscallFailed;
                    }
                }
            }
            message *msg = malloc(sizeof(message) + sizeof(peer_info) * numberOfPeers);
            msg->type = kPeerList;
            msg->peerList.numberOfPeers = numberOfPeers;
            if (numberOfPeers > 0)
                memcpy(&msg->peerList.peers[0], peers, sizeof(peer_info) * numberOfPeers); 
            free(peers);
            *messageOut = msg;
        } break;
    }

    return kSuccess;
}
