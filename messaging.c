#include "p2pjs.h"
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <assert.h>

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
SendQueryJobResources(int fd, uint8 cookie[CookieLen], peer_info info)
{
    uint16 messageType = kQueryJobResources;
    if (SendBytes(fd, sizeof(messageType), (const char*)&messageType) != kSuccess)
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
    uint32 sourceLen = strlen(job->source) + 1;
    if (SendBytes(fd, sizeof(sourceLen), (const char*)&sourceLen) != kSuccess)
    {
        perror("sourceLen");
        return kSyscallFailed;
    }
    if (SendBytes(fd, CookieLen, (const char*)cookie) != kSuccess)
    {
        perror("cookie");
        return kSyscallFailed;
    }
    if (SendBytes(fd, sizeof(double), (const char*)&job->arg) != kSuccess)
    {
        perror("arg");
        return kSyscallFailed;
    }
    if (SendBytes(fd, sourceLen, job->source) != kSuccess)
    {
        perror("source");
        return kSyscallFailed;
    }
    return kSuccess;
}

internal int
SendJobResult(int fd, uint8 cookie[CookieLen], int state, double result)
{
    uint16 messageType = kJobResult; 
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
    if (SendBytes(fd, sizeof(int), (const char*)&state) != kSuccess)
    {
        perror("state");
        return kSyscallFailed;
    }
    if (SendBytes(fd, sizeof(double), (const char*)&result) != kSuccess)
    {
        perror("result");
        return kSyscallFailed;
    }
    return kSuccess;
}

typedef struct
{
    int fd;
    uint16 messageType;
    int targetLength;
    int receivedByteCount;
    unsigned int bufferCapacity;
    char *buffer;
} message_buffer;

#define GetBufferPtr(buffer) ((char*)((buffer)->buffer) + (buffer)->receivedByteCount)

global_variable message_buffer *g_messageBuffers;
global_variable int g_messageBufferCount;

internal int 
ReceiveBytes(int fd, int byteCount, void *_buffer)
{
    char *buffer = (char*)_buffer;
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
ReceiveSomeBytes(int fd, int maxCount, void *_buffer)
{
    char *buffer = (char*)_buffer;
    return recv(fd, buffer, maxCount, 0);
}

internal void
FreeMessage(message *message)
{
    free(message);
}

internal int 
ReceiveMessage(int fd, message **messageOut)
{
    // NOTE(Kevin): Check if we already have data for that fd
    message_buffer *buffer = 0;
    for (int i = 0; i < g_messageBufferCount; ++i)
    {
        if (g_messageBuffers[i].fd == fd)
        {
            buffer = &g_messageBuffers[i];
            break;
        }
    }
    if (buffer && (buffer->targetLength != buffer->receivedByteCount))
    {
        // NOTE(Kevin): message in progress
        WriteToLog("Message in progress on fd %d\n", fd);
        int outstandingBytes = buffer->targetLength - buffer->receivedByteCount;
        switch (buffer->messageType)
        {
            case kHello:
            {
                WriteToLog(" - Hello\n");
                assert(outstandingBytes > 0);
                int byteCount = ReceiveSomeBytes(fd,
                                                 outstandingBytes,
                                                 GetBufferPtr(buffer));
                if (byteCount == -1)
                    return kSyscallFailed;
                buffer->receivedByteCount += byteCount;
                if (buffer->receivedByteCount == buffer->targetLength)
                {
                    message *msg = malloc(sizeof(message));
                    msg->type = kHello;
                    memcpy(msg->hello.port, buffer->buffer, PeerPortLen);
                    *messageOut = msg;
                    return kSuccess;
                }
            } break;

            case kGetPeers:
            {
                assert(!"Unreachable");
            } break;

            case kPeerList:
            { 
                WriteToLog(" - PeerList\n");
                if (buffer->targetLength == -1)
                {
                    WriteToLog(" * List length\n");
                    // NOTE(Kevin): Have not received list length, yet
                    if (buffer->bufferCapacity < sizeof(uint16))
                    {
                        assert(buffer->bufferCapacity == 0);
                        buffer->buffer = malloc(sizeof(uint16));
                        buffer->bufferCapacity = sizeof(uint16);
                    }
                    int byteCount = ReceiveSomeBytes(fd,
                                                     sizeof(uint16) - buffer->receivedByteCount,
                                                     GetBufferPtr(buffer)); 
                    if (byteCount == -1)
                        return kSyscallFailed;
                    buffer->receivedByteCount += byteCount;
                    if (buffer->receivedByteCount == sizeof(uint16))
                    {
                        // NOTE(Kevin): now we know the list lenght
                        uint16 listLength = *(uint16*)buffer->buffer;
                        buffer->targetLength = sizeof(uint16) + sizeof(peer_info) * listLength;
                        buffer->buffer = realloc(buffer->buffer, buffer->targetLength);
                    }
                    return kWouldBlock;
                }
                else
                {
                    WriteToLog(" * List\n");
                    // NOTE(Kevin): Receive bytes until we have the complete list
                    assert(outstandingBytes > 0);
                    int byteCount = ReceiveSomeBytes(fd, outstandingBytes, GetBufferPtr(buffer));
                    if (byteCount == -1)
                        return kSyscallFailed;
                    buffer->receivedByteCount += byteCount;
                    if (buffer->receivedByteCount == buffer->targetLength)
                    {
                        uint16 numberOfPeers = *(uint16*)buffer->buffer;
                        message *msg = malloc(sizeof(message) + sizeof(peer_info) * numberOfPeers);
                        msg->type = kPeerList;
                        msg->peerList.numberOfPeers = numberOfPeers;
                        memcpy(msg->peerList.peers, (char*)buffer->buffer + sizeof(uint16),
                               numberOfPeers * sizeof(peer_info)); 
                        *messageOut = msg;
                        return kSuccess;
                    }
                    else
                    {
                        return kWouldBlock;
                    }
                }
            } break;

            case kQueryJobResources:
            {
                WriteToLog(" - QueryJobResources\n");
                assert(outstandingBytes > 0);
                int byteCount = ReceiveSomeBytes(fd,
                                                 outstandingBytes,
                                                 GetBufferPtr(buffer));
                if (byteCount == -1)
                    return kSyscallFailed;
                buffer->receivedByteCount += byteCount;
                if (buffer->receivedByteCount == buffer->targetLength)
                {
                    message *msg = malloc(sizeof(message));
                    msg->type = kQueryJobResources;
                    memcpy(msg->queryJobResources.cookie, buffer->buffer, CookieLen);
                    memcpy(&msg->queryJobResources.source, (char*)buffer->buffer + CookieLen, sizeof(peer_info));
                    *messageOut = msg;
                    return kSuccess;
                }
            } break;

            case kOfferJobResources:
            {
                WriteToLog(" - OfferJobResources\n");
                assert(outstandingBytes > 0);
                int byteCount = ReceiveSomeBytes(fd,
                                                 outstandingBytes,
                                                 GetBufferPtr(buffer));
                if (byteCount == -1)
                    return kSyscallFailed;
                buffer->receivedByteCount += byteCount;
                if (buffer->receivedByteCount == buffer->targetLength)
                {
                    message *msg = malloc(sizeof(message));
                    msg->type = kOfferJobResources;
                    memcpy(msg->offerJobResources.cookie, buffer->buffer, CookieLen);
                    *messageOut = msg;
                    return kSuccess;
                }
            } break;

            case kJob:
            {
                WriteToLog(" - Job\n");
                if (buffer->targetLength == -1)
                {
                    WriteToLog(" * sourceLen\n");
                    // NOTE(Kevin): Have not received source length, yet
                    if (buffer->bufferCapacity < sizeof(uint32))
                    {
                        assert(buffer->bufferCapacity == 0);
                        buffer->buffer = malloc(sizeof(uint32));
                        buffer->bufferCapacity = sizeof(uint32);
                    }
                    int byteCount = ReceiveSomeBytes(fd,
                                                     sizeof(uint32) - buffer->receivedByteCount,
                                                     GetBufferPtr(buffer));
                    if (byteCount == -1)
                        return kSyscallFailed;
                    buffer->receivedByteCount += byteCount;
                    if (buffer->receivedByteCount == sizeof(uint32))
                    {
                        // NOTE(Kevin): now we know the source lenght
                        uint32 sourceLength = *(uint32*)buffer->buffer;
                        buffer->targetLength = sizeof(uint32) + // NOTE(Kevin): Source length
                                               CookieLen + // NOTE(Kevin): Cookie
                                               sizeof(double) + // NOTE(Kevin): Arg
                                               sourceLength;
                        buffer->buffer = realloc(buffer->buffer, buffer->targetLength);
                    }
                    return kWouldBlock;
                } 
                else
                {
                    WriteToLog(" * details\n");
                    // NOTE(Kevin): Receive bytes until we have the complete job 
                    assert(outstandingBytes > 0);
                    int byteCount = ReceiveSomeBytes(fd, outstandingBytes, GetBufferPtr(buffer));
                    if (byteCount == -1)
                        return kSyscallFailed;
                    buffer->receivedByteCount += byteCount;
                    if (buffer->receivedByteCount == buffer->targetLength)
                    {
                        uint32 sourceLength = *(uint32*)buffer->buffer;
                        message *msg = malloc(sizeof(message) + sourceLength); 
                        msg->type = kJob;
                        msg->job.sourceLen = sourceLength;
                        memcpy(msg->job.cookie, (char*)buffer->buffer + sizeof(uint32), CookieLen);
                        msg->job.arg = *(double*)((char*)buffer->buffer + sizeof(uint32) + CookieLen);
                        memcpy(msg->job.source,
                               (char*)buffer->buffer + sizeof(uint32) + CookieLen + sizeof(double),
                               sourceLength);
                        *messageOut = msg;
                        return kSuccess;
                    }
                    else
                    {
                        return kWouldBlock;
                    }
                }
            } break;

            case kJobResult:
            {
                WriteToLog(" - JobResult\n");
                assert(outstandingBytes > 0);
                int byteCount = ReceiveSomeBytes(fd,
                                                 outstandingBytes,
                                                 GetBufferPtr(buffer));
                if (byteCount == -1)
                    return kSyscallFailed;
                buffer->receivedByteCount += byteCount;
                if (buffer->receivedByteCount == buffer->targetLength)
                {
                    message *msg = malloc(sizeof(message));
                    msg->type = kJobResult;
                    memcpy(msg->jobResult.cookie, buffer->buffer, CookieLen);
                    msg->jobResult.state  = *(int*)((char*)buffer->buffer + CookieLen); 
                    msg->jobResult.result = *(double*)((char*)buffer->buffer + CookieLen + sizeof(int));
                    *messageOut = msg;
                    return kSuccess;
                }
            } break;

            default:
            {
                return kUnknownMessageType;
            } break;
        }
    }
    else
    {
        // NOTE(Kevin): New message
        WriteToLog("Receiving new message on fd %d\n", fd);
        if (!buffer)
        {
            // NOTE(Kevin): Grow the message buffer array
            message_buffer *t = realloc(g_messageBuffers,
                                        sizeof(message_buffer) * (g_messageBufferCount + 1));
            if (!t)
            {
                return kNoMemory;
            }
            g_messageBuffers = t;
            buffer = &g_messageBuffers[g_messageBufferCount];
            buffer->buffer = 0;
            ++g_messageBufferCount;
        } 
        free(buffer->buffer);
        buffer->fd = fd;
        buffer->targetLength = -1; // NOTE(Kevin): We don't know yet
        buffer->receivedByteCount = 0;
        buffer->buffer = 0;
        buffer->bufferCapacity = 0;

        if (ReceiveBytes(fd, sizeof(uint16), &buffer->messageType) != kSuccess)
        {
            return kSyscallFailed;
        }

        switch (buffer->messageType)
        {
            case kHello:
            {
                buffer->targetLength = PeerPortLen; // NOTE(Kevin): Port
            } break;

            case kGetPeers:
            {
                buffer->targetLength = 0; // NOTE(Kevin): No content
            } break;

            case kQueryJobResources:
            {
                buffer->targetLength = CookieLen + // NOTE(Kevin): Cookie
                                       sizeof(peer_info); // NOTE(Kevin): Source
            } break;

            case kOfferJobResources:
            {
                buffer->targetLength = CookieLen; // NOTE(Kevin): Cookie
            } break;

            case kJobResult:
            {
                buffer->targetLength = CookieLen + // NOTE(Kevin): Cookie
                                       sizeof(int) + // NOTE(Kevin): State
                                       sizeof(double); // NOTE(Kevin): Result
            } break;
        };

        if (buffer->targetLength == buffer->receivedByteCount)
        {
            // NOTE(Kevin): Done
            assert(buffer->messageType == kGetPeers);
            message *msg = malloc(sizeof(message));
            msg->type = buffer->messageType;
            *messageOut = msg;
            return kSuccess;
        }
        else
        {
            if (buffer->targetLength > 0)
            {
                buffer->buffer = malloc(buffer->targetLength);
                buffer->bufferCapacity = buffer->targetLength;
            }
            return kWouldBlock;
        }
#if 0

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
                                double arg;
                                if (ReceiveBytes(fd, sizeof(double), (char*)&arg) != kSuccess)
                                    return kSyscallFailed;
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
                                memcpy(msg->job.cookie, cookie, CookieLen);
                                msg->job.cSource.arg        = arg;
                                msg->job.cSource.sourceLen  = sourceLen;
                                msg->job.cSource.source     = source;
                                *messageOut = msg;
                            } break;
                    }
                } break;


            case kJobResult:
                {
                    uint8 cookie[CookieLen];
                    if (ReceiveBytes(fd, CookieLen, (char*)cookie) != kSuccess)
                        return kSyscallFailed;
                    int state;
                    if (ReceiveBytes(fd, sizeof(int), (char*)&state) != kSuccess)
                        return kSyscallFailed;
                    double result;
                    if (ReceiveBytes(fd, sizeof(double), (char*)&result) != kSuccess)
                        return kSyscallFailed;
                    message *msg = malloc(sizeof(message));
                    msg->type = kJobResult;
                    msg->jobResult.result = result;
                    msg->jobResult.state  = state;
                    memcpy(msg->jobResult.cookie, cookie, CookieLen);
                    *messageOut = msg;
                } break;

            default:
                {
                    return kInvalidValue;
                } break;
        }
#endif
    }
    return kSuccess;
}
