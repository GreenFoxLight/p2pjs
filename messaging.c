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
SendQueryJobResources(int fd, uint32 jobType, uint8 cookie[CookieLen], peer_info info)
{
    uint16 messageType = kQueryJobResources;
    if (SendBytes(fd, sizeof(messageType), (const char*)&messageType) != kSuccess)
        return kSyscallFailed;
    if (SendBytes(fd, sizeof(jobType), (const char*)&jobType) != kSuccess)
        return kSyscallFailed;
    if (SendBytes(fd, CookieLen, (const char*)cookie) != kSuccess)
        return kSyscallFailed;
    return SendBytes(fd, sizeof(info), (const char*)&info);
}

internal int
SendOfferJobResources(int fd, uint8 cookie[CookieLen])
{
    uint16 messageType = kOfferJobResources;
    if (SendBytes(fd, sizeof(messageType), (const char*)&messageType) != kSuccess)
        return kSyscallFailed;
    return SendBytes(fd, CookieLen, (const char*)cookie);
}

internal int
SendJob(int fd, uint8 cookie[CookieLen], const job *job)
{
    uint16 messageType = kJob;
    if (SendBytes(fd, sizeof(messageType), (const char*)&messageType) != kSuccess)
    {
        perror("messageType");
        return kSyscallFailed;
    }
    if (SendBytes(fd, CookieLen, (const char*)cookie) != kSuccess)
    {
        perror("cookie");
        return kSyscallFailed;
    }
    if (SendBytes(fd, sizeof(job->type), (const char*)&job->type) != kSuccess)
    {
        perror("jobType");
        return kSyscallFailed;
    }
    if (job->type == kJobCSource)
    {
        uint32 sourceLen = strlen(job->cSource.source) + 1;
        if (SendBytes(fd, sizeof(sourceLen), (const char*)&sourceLen) != kSuccess)
        {
            perror("sourceLen");
            return kSyscallFailed;
        }
        if (SendBytes(fd, sourceLen, job->cSource.source) != kSuccess)
        {
            perror("source");
            return kSyscallFailed;
        }
        return kSuccess;
    }
    return kInvalidValue; 
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

internal void
FreeMessage(message *message)
{
    if (message->type == kJob)
    {
        if (message->job.type == kJobCSource)
            free(message->job.cSource.source);
    }
    free(message);
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
        case kHello:
        {
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

        case kGetPeers:
        {
            // NOTE(Kevin): Get peers has no data attached
            message *msg = malloc(sizeof(message));
            if (!msg)
                return kNoMemory;
            msg->type = kGetPeers;
            *messageOut = msg;
        } break;

        case kPeerList:
        {
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

        case kQueryJobResources:
        {
            uint32 jobType;
            if (ReceiveBytes(fd, sizeof(jobType), (char*)&jobType) != kSuccess)
            {
                return kSyscallFailed;
            }
            uint8 cookie[CookieLen];
            if (ReceiveBytes(fd, CookieLen, (char*)cookie) != kSuccess)
            {
                return kSyscallFailed;
            }
            peer_info source;
            if (ReceiveBytes(fd, sizeof(peer_info), (char*)&source) != kSuccess)
            {
                return kSyscallFailed;
            }
            message *msg = malloc(sizeof(message));
            msg->type = kQueryJobResources;
            msg->queryJobResources.type     = jobType;
            memcpy(&msg->queryJobResources.cookie[0], cookie, CookieLen);
            msg->queryJobResources.source   = source;
            *messageOut = msg;
        } break;

        case kOfferJobResources:
        {
            uint8 cookie[CookieLen];
            if (ReceiveBytes(fd, CookieLen, (char*)cookie) != kSuccess)
                return kSyscallFailed;
            message *msg = malloc(sizeof(message));
            msg->type = kOfferJobResources;
            memcpy(&msg->offerJobResources.cookie[0], cookie, CookieLen);
            *messageOut = msg;
        } break;

        case kJob:
        {
            uint8 cookie[CookieLen];
            if (ReceiveBytes(fd, CookieLen, (char*)cookie) != kSuccess)
                return kSyscallFailed;
            uint32 jobType;
            if (ReceiveBytes(fd, sizeof(uint32), (char*)&jobType) != kSuccess)
                return kSyscallFailed; 
            switch (jobType)
            {
                case kJobCSource:
                {
                    uint32 sourceLen = 0;
                    if (ReceiveBytes(fd, sizeof(uint32), (char*)&sourceLen) != kSuccess)
                        return kSyscallFailed;
                    printf("Source len: %d\n", sourceLen);
                    char *source = malloc(sourceLen);
                    if (!source)
                        return kNoMemory;
                    if (ReceiveBytes(fd, sourceLen, source) != kSuccess)
                    {
                        free(source);
                        return kSyscallFailed;
                    }
                    message *msg = malloc(sizeof(message));
                    msg->type                   = kJob;
                    msg->job.type               = jobType;
                    msg->job.cSource.sourceLen  = sourceLen;
                    msg->job.cSource.source     = source;
                    *messageOut = msg;
                } break;
                
                default:
                {
                    return kInvalidValue;
                } break;
            }
        } break;
    }

    return kSuccess;
}
