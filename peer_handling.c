#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "p2pjs.h"

typedef struct
{
    int fd;
    int id; 
} ready_peer;

typedef struct
{
    int fd;
    int id;
    bool32 hasIncomingData;
} closed_peer;

global_variable struct pollfd *g_peerFds;
global_variable peer_info *g_peerInfo;
global_variable unsigned int g_peerCount;
global_variable unsigned int g_peerCapacity;

internal peer_iterator
GetFirstPeer(void)
{
    if (g_peerCount == 0)
    {
        peer_iterator p = { .fd = 0, .id = -1 };
        return p;
    }
    else
    {
        peer_iterator p = { .fd = g_peerFds[0].fd, .id = 0 };
        return p;
    }
}

internal void 
GetNextPeer(peer_iterator *p)
{
    if (p->id == -1)
    {
        return;
    }
    else
    {
        ++p->id;
        if (p->id < (int)g_peerCount)
            p->fd = g_peerFds[p->id].fd;
        else
            p->id = -1;
    }
}

internal bool32
IsBehindLastPeer(peer_iterator *p)
{
    return p->id == -1;
}

internal int 
AddPeer(int fd, const char *ipAddress)
{
    unsigned int newCount = g_peerCount + 1;
    if (g_peerCount == g_peerCapacity) {
        unsigned int newCapacity = g_peerCapacity > 0 ? g_peerCapacity * 2 : 8;
        struct pollfd *t = realloc(g_peerFds, sizeof(struct pollfd) * newCapacity);
        if (!t)
            return -1;
        g_peerFds = t;
        peer_info *i = realloc(g_peerInfo, sizeof(peer_info) * newCapacity);
        if (!i)
            return -1;
        g_peerInfo = i;
        g_peerCapacity = newCapacity;
    }
    g_peerCount = newCount;

    g_peerFds[newCount - 1].fd      = fd;
    g_peerFds[newCount - 1].events  = POLLIN;
    g_peerFds[newCount - 1].revents = 0;
    // NOTE(Kevin): We don't know the port, yet; but we know the ip address
    strncpy(g_peerInfo[newCount - 1].ipaddr, ipAddress, PeerIPLen);
    g_peerInfo[newCount - 1].port[0] = '\0';

    return newCount - 1;
}

internal void
RemovePeers(closed_peer *closedPeers, unsigned int closedPeerCount)
{
    // NOTE(Kevin): This works, because closedPeers is sorted by id (by design)
    for (int i = (int)closedPeerCount - 1; i >= 0; --i)
    {
        if ((unsigned int)closedPeers[i].id == g_peerCount - 1)
        {
            // NOTE(Kevin): Just drop the peer
            --g_peerCount;
        }
        else
        {
            // NOTE(Kevin): The last peer in the list MUST be open
            unsigned int myId   = closedPeers[i].id;
            g_peerFds[myId]     = g_peerFds[g_peerCount - 1];
            g_peerInfo[myId]    = g_peerInfo[g_peerCount - 1];
            --g_peerCount;
        }
    }
}

internal void
UpdatePeerPort(int peerId, const char *port)
{
    assert(peerId < (int)g_peerCount);
    strncpy(g_peerInfo[peerId].port, port, PeerPortLen);
}

internal const char*
GetPeerIP(int peerId)
{
    if (peerId < (int)g_peerCount)
        return g_peerInfo[peerId].ipaddr;
    return 0;
}

internal int
GetPeerFd(int id)
{
    if (id < (int)g_peerCount)
        return g_peerFds[id].fd;
    return -1;
}

internal int
AreIPAddressesEqual(const char *a, const char *b)
{
    if (strcmp(a, b) == 0)
        return 1;
    unsigned int prefixLen = strlen("::ffff:");
    if (strlen(a) >= prefixLen)
    {
        if (strncmp(a, "::ffff:", prefixLen) == 0)
        {
            return strcmp(&a[prefixLen], b) == 0;
        }
    }
    if (strlen(b) >= prefixLen)
    {
        if (strncmp(b, "::ffff:", prefixLen) == 0)
        {
            return strcmp(a, &b[prefixLen]) == 0;
        }
    }
    return 0;
}

internal int
CheckForPeer(const char *ip, const char *port)
{
    for (unsigned int i = 0; i < g_peerCount; ++i)
    {
        // NOTE(Kevin): IPv4 addresses are (sometimes) written
        // as IPv6 addresses ::ffff:<ADDR>
        if (AreIPAddressesEqual(g_peerInfo[i].ipaddr, ip) &&
            strcmp(g_peerInfo[i].port, port) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

internal int 
CheckPeerStatus(ready_peer **readyPeersOut,
                unsigned int *readyPeerCountOut,
                closed_peer **closedPeersOut,
                unsigned int *closedPeerCountOut)
{
    ready_peer *readyPeers = 0;
    closed_peer *closedPeers = 0;
    unsigned int readyPeerCount = 0, closedPeerCount = 0;
    // NOTE(Kevin): Clear the revents fields
    for (unsigned int i = 0; i < g_peerCount; ++i)
    {
        g_peerFds[i].revents = 0;
    }
    int result = poll(g_peerFds, (nfds_t)g_peerCount, 0);
    if (result > 0)
    {
        // NOTE(Kevin): At least one descriptor is ready
        for (unsigned int i = 0; i < g_peerCount; ++i)
        {
            WriteToLog("%d: %d\n", i, g_peerFds[i].revents);
            if ((g_peerFds[i].revents & POLLHUP) != 0)
            {
                closed_peer *t = realloc(closedPeers,
                                         sizeof(closed_peer) * (closedPeerCount + 1));
                if (!t) return kNoMemory;
                closedPeers = t;
                closedPeers[closedPeerCount].fd = g_peerFds[i].fd;
                closedPeers[closedPeerCount].id = (int)i;
                closedPeers[closedPeerCount].hasIncomingData = g_peerFds[i].revents & POLLIN;
                ++closedPeerCount;
            }
            else if ((g_peerFds[i].revents & POLLIN) != 0)
            {
                ready_peer *t = realloc(readyPeers,
                                         sizeof(ready_peer) * (readyPeerCount + 1));
                if (!t) return kNoMemory;
                readyPeers = t;
                readyPeers[readyPeerCount].fd = g_peerFds[i].fd;
                readyPeers[readyPeerCount].id = (int)i;
                ++readyPeerCount;
            }
        } 
    }
    else if (result == -1)
    {
        // NOTE(Kevin): Error
        return kSyscallFailed;
    }
    *readyPeersOut      = readyPeers;
    *closedPeersOut     = closedPeers;
    *readyPeerCountOut  = readyPeerCount;
    *closedPeerCountOut = closedPeerCount;
    return kSuccess;
}

internal int 
ConnectToPeer(const char *ip, const char *port, const char *myPort, bool32 getPeerList)
{
    struct addrinfo hints, *res;
    int peerFd = 0, peerId = -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family     = AF_UNSPEC;
    hints.ai_socktype   = SOCK_STREAM;
    getaddrinfo(ip, port, &hints, &res);
    
    WriteToLog("Connecting to %s : %s\n", ip, port);
    peerFd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (peerFd >= 0)
    {
        if (connect(peerFd, res->ai_addr, res->ai_addrlen) == 0)
        {
            peerId = AddPeer(peerFd, ip);
            if (peerId > -1)
            {
                UpdatePeerPort(peerId, port);
                if (SendHello(peerFd, myPort) != kSuccess)
                {
                    WriteToLog("Failed to send hello message to peer %s : %s.\n", ip, port);
                    close(peerFd);
                    closed_peer forceClose;
                    forceClose.fd = peerFd;
                    forceClose.id = peerId;
                    RemovePeers(&forceClose, 1);
                }

                if (getPeerList)
                {
                    if (SendGetPeers(peerFd) != kSuccess)
                    {
                        WriteToLog("Failed to send getPeers message to peer %s : %s.\n", ip, port);
                        close(peerFd);
                        closed_peer forceClose;
                        forceClose.fd = peerFd;
                        forceClose.id = peerId;
                        RemovePeers(&forceClose, 1);
                    }
                }
            }
        }
        else
        {
            perror("Connect: ");
            WriteToLog("Failed to connect to peer %s : %s.\n", ip, port);
            close(peerFd);
        }
    }
    else
    {
        WriteToLog("Failed to open socket for peer connection.\n");
    }
    if (peerId != -1)
        WriteToLog("Established connection to %s: %s. Assigned id %d.\n", ip, port, peerId);
    return peerId;
}

internal int GetNumberOfRunningJobs(void);
internal int SendJobToPeer(uint8 cookie[CookieLen], int peerFd);
internal int TakeJob(uint8 cookie[CookieLen], job theJob, int peerId);
internal const char* CookieToTemporaryString(uint8 cookie[CookieLen]);
internal int StoreJobResult(uint8 cookie[CookieLen], int state, double result);

internal void
HandleMessageFromPeer(int fd, int id, const char *myPort)
{
    message *message;
    int err;
    if ((err = ReceiveMessage(fd, &message)) == kSuccess)
    {
        switch (message->type)
        {
            case kHello:
            {
                WriteToLog("Received hello message from peer %d [%s].\n",
                            id, GetPeerIP(id));
                UpdatePeerPort(id, message->hello.port);
            } break;

            case kGetPeers:
            {
                WriteToLog("Received getPeers message from peer %d [%s].\n",
                           id, GetPeerIP(id));
                // NOTE(Kevin): Gather peer list
                peer_info *peerList = malloc(sizeof(peer_info) * g_peerCount);
                if (peerList)
                {
                    unsigned int listLength = 0;
                    for (unsigned int i = 0; i < g_peerCount; ++i)
                    {
                        if (g_peerInfo[i].port[0] == '\0')
                            continue; // NOTE(Kevin): Incomplete peer info
                        else if (i == (unsigned int)id)
                            continue; // NOTE(Kevin): Don't send the peers info
                        else
                        {
                            strncpy(peerList[listLength].ipaddr, g_peerInfo[i].ipaddr, PeerIPLen);
                            strncpy(peerList[listLength].port, g_peerInfo[i].port, PeerPortLen);
                            ++listLength;
                        }
                    } 
                    SendPeerList(fd, listLength, peerList);
                    free(peerList);
                }
                else
                {
                    WriteToLog("Can not anwser getPeers message from peer %d [%s], because malloc failed.\n",
                               id, GetPeerIP(id));
                }
            } break;
            
            case kPeerList:
            {
                WriteToLog("Received peerList message from peer %d [%s].\n",
                           id, GetPeerIP(id));
                for (uint16 i = 0; i < message->peerList.numberOfPeers; ++i)
                {
                    WriteToLog(" * Entry %d: IP: %s, Port: %s\n",
                               i,
                               message->peerList.peers[i].ipaddr,
                               message->peerList.peers[i].port);

                    int id = CheckForPeer(message->peerList.peers[i].ipaddr,
                                          message->peerList.peers[i].port);
                    if (id == -1)
                    {
                        // NOTE(Kevin): New peer
                        bool32 getList = rand() % 2;
                        id = ConnectToPeer(message->peerList.peers[i].ipaddr,
                                           message->peerList.peers[i].port,
                                           myPort, getList);
                        if (id == -1)
                        {
                            WriteToLog("Failed to connect to peer %s %s\n",
                                       message->peerList.peers[i].ipaddr,
                                       message->peerList.peers[i].port);
                        }
                    }
                }
            } break;

            case kQueryJobResources:
            {
                WriteToLog("Received queryJobResources message from peer %d [%s].\n",
                           id, GetPeerIP(id));
                // NOTE(Kevin): Decide if we want to take the job

                if (GetNumberOfRunningJobs() < P2PJS_MaxRunningJobs)
                {
                    WriteToLog("Offering to take the job.\n");
                    // NOTE(Kevin): Offer to take the job
                    int id = CheckForPeer(message->queryJobResources.source.ipaddr,
                                          message->queryJobResources.source.port);
                    if (id == -1)
                    {
                        WriteToLog("Attempting to connect to source %s %s.\n",
                                   message->queryJobResources.source.ipaddr,
                                   message->queryJobResources.source.port);
                        // NOTE(Kevin): New peer
                        id = ConnectToPeer(message->queryJobResources.source.ipaddr,
                                           message->queryJobResources.source.port,
                                           myPort, 0);
                        if (id == -1)
                        {
                            WriteToLog("Failed to connect to peer %s %s\n",
                                       message->queryJobResources.source.ipaddr,
                                       message->queryJobResources.source.port);
                        }
                    }
                    if (id > -1)
                    {
                        int fd = g_peerFds[id].fd;
                        if (SendOfferJobResources(fd, message->queryJobResources.cookie) != kSuccess)
                        {
                            WriteToLog("Failed to send offer.\n");
                        }
                    }
                }
                else
                {
                    // NOTE(Kevin): Spread the message
                    for (peer_iterator peer = GetFirstPeer();
                         !IsBehindLastPeer(&peer);
                         GetNextPeer(&peer))
                    {
                        if (peer.id == id)
                            continue;
                        WriteToLog("Spreading to peer %d [%s].\n",
                                   peer.id, GetPeerIP(peer.id));
                        SendQueryJobResources(peer.fd,
                                              message->queryJobResources.cookie,
                                              message->queryJobResources.source);
                    }
                }
            } break;

            case kOfferJobResources:
            {
                WriteToLog("Received offerJobResources message from peer %d [%s].\n",
                           id, GetPeerIP(id));
                WriteToLog("Sending job to peer %d [%s].\n", id, GetPeerIP(id)); 
                int err;
                if ((err = SendJobToPeer(message->offerJobResources.cookie, fd)) != kSuccess)
                {
                    WriteToLog("Failed: %s\n", ErrorToString(err));
                }
            } break;

            case kJob:
            {
                WriteToLog("Received job message from peer %d [%s].\n",
                           id, GetPeerIP(id));

                WriteToLog("Job has cookie %s\n", CookieToTemporaryString(message->job.cookie));
                job theJob = {
                    .source = message->job.source,
                    .arg    = message->job.arg,
                };

                int err;
                if ((err = TakeJob(message->job.cookie, theJob, id)) != kSuccess)
                {
                    WriteToLog("Failed to take job: %s\n", ErrorToString(err));
                }
            } break;

            case kJobResult:
            {
                WriteToLog("Received jobResult message from peer %d [%s].\n",
                           id, GetPeerIP(id));
                WriteToLog("Result is for job %s\n",
                           CookieToTemporaryString(message->jobResult.cookie));
                StoreJobResult(message->jobResult.cookie, message->jobResult.state, message->jobResult.result);
            } break;

            default:
            {
                WriteToLog("Received message from peer %d [%s] with unkown message type 0x%x\n",
                           id, GetPeerIP(id), message->type);
            } break;
        }
        FreeMessage(message);
    }
    else
    {
        if (err != kWouldBlock)
            WriteToLog("Receive message failed: %s\n", ErrorToString(err));
    }
}
