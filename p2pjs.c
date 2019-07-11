#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>

#include "p2pjs.h"

internal const char*
ErrorToString(int error)
{
    local_persist const char *strings[] =
    {
        "Success",
        "NoMemory",
        "SyscallFailed",
        "WouldBlock",
        "InvalidValue",
        "NoAvailableInterface",
        "CouldNotOpenFile",
        "JobNotFound",
        "CompileError",
        "RuntimeError",
        "UnknownMessageType",
    }; 
    return strings[error];
}

internal const char *g_localIp;
internal const char *g_localPort;
internal int g_serverFd;

#include "getlocalip.c"
#include "logging.c"
#include "vm.c"
#include "messaging.c"
#include "peer_handling.c"
#include "jobs.c"
#include "ui.c"

#define DefaultPort "2096"

internal char*
GetIPAddressString(const struct sockaddr *sa, char *s, size_t maxlen)
{
    switch(sa->sa_family)
    {
        case AF_INET:
            inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
                    s, maxlen);
            break;

        case AF_INET6:
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
                    s, maxlen);
            break;

        default:
            strncpy(s, "Unknown AF", maxlen);
            return NULL;
    }
    return s;
}

internal int
OpenServerSocket(const char *port)
{
    // NOTE(Kevin): Open the server socket
    int status;
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *addr;
    if ((status = getaddrinfo(0, port, &hints, &addr)) != 0)
    {
        fprintf(stderr, "Failed to getaddrinfo(); status=%s\n", gai_strerror(status));
        return -1;
    } 
    int serverFd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (serverFd == -1)
    {
        fprintf(stderr, "Failed to open server socket.\n");
        return -1;
    }
    bind(serverFd, addr->ai_addr, addr->ai_addrlen);
    // NOTE(Kevin): Make that socket reusable
    int yes = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
    {
        fprintf(stderr, "Failed to make server socket reusable.\n");
    }

    // NOTE(Kevin): Make the socket non-blocking
    if (fcntl(serverFd, F_SETFL, fcntl(serverFd, F_GETFL, 0) | O_NONBLOCK) == -1)
    {
        fprintf(stderr, "Failed to make server socket non-blocking.\n");
    }

    freeaddrinfo(addr);
    return serverFd;
}

global_variable bool32 g_shouldExit;

void
OnExit(int s)
{
    Unused(s);
    WriteToLog("Exiting!\n");
    g_shouldExit = 1;
}

internal void
Frame(void)
{
    struct sockaddr_storage clientAddress;
    socklen_t clientAddressSize = sizeof(clientAddress);
    int clientFd = accept(g_serverFd, (struct sockaddr*)&clientAddress, &clientAddressSize);
    if (clientFd != -1) {
        char ipAddressString[INET6_ADDRSTRLEN];
        GetIPAddressString((struct sockaddr*)&clientAddress,
                ipAddressString,
                SizeofArray(ipAddressString));
        WriteToUser("Got incoming connection from %s.\n", ipAddressString);

        // NOTE(Kevin): Add to list of known peers 
        int id = AddPeer(clientFd, ipAddressString);
        if (id > -1)
        {
            WriteToLog("Peer with ip %s got id %d.\n", ipAddressString, id);
        }
        else
        {
            WriteToLog("Failed to add to list of known peers.\n");
        }
    }

    ready_peer *readyPeers;
    closed_peer *closedPeers;
    unsigned int readyPeerCount, closedPeerCount;
    if (CheckPeerStatus(&readyPeers, &readyPeerCount,
                &closedPeers, &closedPeerCount) == kSuccess)
    {
        for (unsigned int i = 0; i < readyPeerCount; ++i)
        {
            WriteToLog("Incoming data from peer %d [%s].\n",
                    readyPeers[i].id,
                    GetPeerIP(readyPeers[i].id));
            HandleMessageFromPeer(readyPeers[i].fd, readyPeers[i].id, g_localPort);
        } 
        for (unsigned int i = 0; i < closedPeerCount; ++i)
        {
            WriteToUser("Peer %d [%s] has closed the connection.\n",
                    closedPeers[i].id,
                    GetPeerIP(closedPeers[i].id));
            // TODO(Kevin): Handle outstanding messages
            // We need a way to TRY to handle messages, because
            // the message might be incomplete
            close(closedPeers[i].fd);
        }
        RemovePeers(closedPeers, closedPeerCount);
        free(readyPeers);
        free(closedPeers);
    }
    else
    {
        WriteToLog("CheckPeerStatus() failed.\n"); 
    }
}

int
main(int argc, char **argv)
{
    const char *port = DefaultPort;
    bool32 forkToBackground = 0;
    char *firstPeer = 0;
    // NOTE(Kevin): Parse command line arguments
    int option = '?';
    char *scriptPath  = 0;

    while ((option = getopt(argc, argv, "p:f:bs:")) != -1)
    {
        switch (option)
        {
            case 'p':
            {
                // NOTE(Kevin): Port
                char *portBuffer = malloc(strlen(optarg) + 1);
                strcpy(portBuffer, optarg);
                port = portBuffer;
            } break;
            case 'f':
            {
                // NOTE(Kevin): First peer
                firstPeer = malloc(strlen(optarg) + 1);
                strcpy(firstPeer, optarg);
            } break;
            case 'b':
            {
                // NOTE(Kevin): Fork to background
                forkToBackground = 1;
            } break;
            case 's':
            {
                scriptPath = malloc(strlen(optarg) + 1);
                strcpy(scriptPath, optarg);
            } break;
            case '?':
            default:
            {
                printf("Usage: %s [-p port] [-f ip#port] [-s path] [-b]\n", argv[0]);
                return 1;
            } break;
        }
    }

    WriteToUser("Started with options: Port %s; First Peer: %s; Fork to Background: %s\n",
               port, (firstPeer) ? firstPeer : "<none>", (forkToBackground) ? "yes" : "no");
    
    if (forkToBackground)
    {
        pid_t pid = fork();
        if (pid == -1)
        {
            fprintf(stderr, "Failed to fork to background.\n");
            return 1;
        }
        if (pid > 0)
        {
            // NOTE(Kevin): Parent forks to background
            return 0;
        }
    }
    
    // NOTE(Kevin): Make sure that on SIGTERM our cleanup code runs
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = OnExit;
    sigaction(SIGTERM, &act, 0);
    sigaction(SIGINT, &act, 0);

    if (!OpenLog()) {
        fprintf(stderr, "Failed to open log file.\n");
        return 1;
    }

    // NOTE(Kevin): Init rand()
    srand((unsigned int)time(0));

    // NOTE(Kevin): Determine local ip address
    char localIp[PeerIPLen];
    if (GetLocalIPAddress(localIp, SizeofArray(localIp)) != kSuccess)
    {
        WriteToUser("Failed to determine local ip address.\n");
        CloseLog();
        return 1;
    }

    WriteToUser("Using %s as local ip.\n", localIp);
    
    g_localPort = port; 
    g_localIp = localIp;

    int serverFd = OpenServerSocket(port);
    WriteToLog("Listening on %s\n", port);
    if (listen(serverFd, 5) == -1) {
        WriteToLog("Can not listen on server socket.\n");
        close(serverFd);
        CloseLog();
        return 1;
    }
    g_serverFd = serverFd;

    if (firstPeer)
    {
        // NOTE(Kevin): Attempt to connect
        char *ip = firstPeer;
        char *fpPort = firstPeer;
        // TODO(Kevin): Check for malformed input
        if (*ip == '#')
        {
            printf("Expected ip#port\n");
            close(serverFd);
            CloseLog();
            return 1;
        }
        while (*fpPort != '#')
        {
            if (*fpPort == '\0')
            {
                printf("Expected ip#port\n");
                close(serverFd);
                CloseLog();
                return 1;
            }
            ++fpPort;
        }
        *fpPort = '\0';
        ++fpPort;
        if (*fpPort == '\0')
        {
            printf("Expected ip#port\n");
            close(serverFd);
            CloseLog();
            return 1;
        }
        ConnectToPeer(ip, fpPort, port, 1);
    }


    if (scriptPath)
    {
        WriteToUser("Executing script %s\n", scriptPath);
        int result = RunScript(scriptPath); 
        WriteToUser("Result: %s\n", ErrorToString(result));
    }
    else
    {
        if (!forkToBackground) {
            if (StartUIThread() != kSuccess)
            {
                WriteToLog("Failed to start ui thread.\n");
                close(serverFd);
                CloseLog();
                return 1;
            }
        }

        while (!g_shouldExit)
        {
            Frame();

            if (!forkToBackground)
            {
                user_command *cmd = GetNextCommand();
                if (cmd)
                {
                    switch (cmd->type)
                    {
                        case kCmdJobCSource:
                        {
                            WriteToLog("CSOURCE COMMAND %s\n", cmd->cSource.path);
                            int result = EmitCSourceJob(cmd->cSource.path,
                                        cmd->cSource.arg,
                                        localIp, port, 0);
                            if (result == kCouldNotOpenFile)
                            {
                                printf("Could not read file %s\n", cmd->cSource.path);
                            }
                            else if (result == kNoMemory)
                            {
                                printf("Not enough memory!\n");
                            }
                        } break;

                        case kCmdQuit:
                        {
                            g_shouldExit = 1;
                        } break;

                        default:
                        {
                            WriteToUser("Unknown command %d\n", cmd->type);
                        } break;
                    }
                    FreeCommand(cmd);
                }
            } 
            
            ExecuteNextJob();
        }

        if (!forkToBackground)
        {
            StopUIThread();
        }
    }

    close(serverFd);


    CloseLog();

    return 0;
}
