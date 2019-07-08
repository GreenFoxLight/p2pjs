#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "p2pjs.h"
#include "sha-256.h"

enum
{
    kStateQuerySent,

    kStateRunning,

    kStateFinished,
};

typedef struct
{
    uint8       cookie[CookieLen];
    peer_info   source; 
    int         state;
    job         job;
} received_job;

typedef struct
{
    uint8       cookie[CookieLen];
    int         state; 
    job         job;
} emitted_job;

global_variable received_job *g_receivedJobs;
global_variable emitted_job  *g_emittedJobs;

global_variable unsigned int g_receivedJobCount;
global_variable unsigned int g_receivedJobCapacity;
global_variable unsigned int g_emittedJobCount;
global_variable unsigned int g_emittedJobCapacity;

internal int 
EmitCSourceJob(const char *sourcePath,
               const char *myIp, const char *myPort)
{
    FILE *file = fopen(sourcePath, "r");
    if (!file)
    {
        return kCouldNotOpenFile;
    }

    char *source = malloc(100);
    unsigned int sourceCapacity = 100;
    unsigned int sourceLength   = 0;
    if (!source)
    {
        return kNoMemory;
    }
    while (!feof(file))
    {
        source[++sourceLength] = fgetc(file);
        if (sourceLength == sourceCapacity)
        {
            char *t = realloc(source, sourceCapacity * 2);
            if (!t)
            {
                free(source);
                fclose(file);
                return kNoMemory;
            }
            source = t;
            sourceCapacity *= 2;
        }
    }
    source[sourceLength] = '\0';
    fclose(file);

    // NOTE(Kevin): Generate a random cookie
    uint32 random = (uint32)rand();
    uint8  cookie[CookieLen];
    calc_sha_256(cookie, &random, sizeof(random));

    peer_info info;
    strncpy(info.ipaddr, myIp, PeerIPLen);
    strncpy(info.port, myPort, PeerPortLen);

    for (int i = 0; i < CookieLen; ++i)
    {
        printf("%x", cookie[i]);
    }
    printf("\n");

    // NOTE(Kevin): Send a message asking for compute resources
    for (peer_iterator peer = GetFirstPeer(); !IsBehindLastPeer(&peer); GetNextPeer(&peer))
    {
        WriteToLog("Sending queryJobResources message to peer %d [%s].\n",
                   peer.id,
                   GetPeerIP(peer.id));
        if (SendQueryJobResources(peer.fd, kJobCSource, cookie, info) != kSuccess)
        {
            WriteToLog(" * Failed!\n");
        }
    }

    if (g_emittedJobCount == g_emittedJobCapacity)
    {
        unsigned int newCapacity = (g_emittedJobCapacity == 0) ? 8 : 2 * g_emittedJobCapacity;
        emitted_job *t = realloc(g_emittedJobs, sizeof(emitted_job) * newCapacity);
        if (!t)
        {
            free(source);
            return kNoMemory;
        }
        g_emittedJobs = t;
        g_emittedJobCapacity = newCapacity;
    }
    memcpy(g_emittedJobs[g_emittedJobCount].cookie, cookie, CookieLen);
    g_emittedJobs[g_emittedJobCount].state = kStateQuerySent;
    g_emittedJobs[g_emittedJobCount].job.type = kJobCSource;
    g_emittedJobs[g_emittedJobCount].job.cSource.source = source;
    ++g_emittedJobCount;
    return kSuccess;
}

internal int
SendJobToPeer(uint8 cookie[CookieLen], int peerFd)
{
    for (unsigned int i = 0; i < g_emittedJobCount; ++i)
    {
        if (memcmp(g_emittedJobs[i].cookie, cookie, CookieLen) == 0)
        {
            // NOTE(Kevin): Only send the job out if it's not already running
            if (g_emittedJobs[i].state == kStateQuerySent)
            {
                int err = SendJob(peerFd, cookie, &g_emittedJobs[i].job); 
                if (err != kSuccess)
                {
                    WriteToLog("SendJob: %d\n", ErrorToString(err));
                }
                return err;
            }
        }
    }
    return kJobNotFound;
}

internal int
GetNumberOfRunningJobs(void)
{
    int count = 0;
    for (unsigned int i = 0; i < g_receivedJobCount; ++i)
    {
        if (g_receivedJobs[i].state == kStateRunning)
            ++count;
    }
    return count;
}
