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
    int         source; 
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

#define HexDigitToChar(D) (((D) >= 10) ? 'a' + ((D) - 10) : '0' + (D))
internal const char*
CookieToTemporaryString(uint8 cookie[CookieLen])
{
    local_persist char cookieString[2 * CookieLen + 1];
    for (int i = 0; i < CookieLen; ++i)
    {
        cookieString[2 * i]     = HexDigitToChar(cookie[i] >> 4);
        cookieString[2 * i + 1] = HexDigitToChar(cookie[i] & 0xf);
    }
    cookieString[2 * CookieLen] = '\0'; 
    return cookieString;
}

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

    WriteToLog("Created job %s\n", CookieToTemporaryString(cookie));

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

internal int 
TakeJob(uint8 cookie[CookieLen], job theJob, int sourceId)
{
    if (g_receivedJobCount == g_receivedJobCapacity)
    {
        unsigned int newCapacity = (g_receivedJobCapacity == 0) ? 8 : 2 * g_receivedJobCapacity;
        received_job *t = realloc(g_receivedJobs, sizeof(received_job) * newCapacity);
        if (!t)
            return kNoMemory;
        g_receivedJobs = t;
        g_receivedJobCapacity = newCapacity;
    }
    memcpy(g_receivedJobs[g_receivedJobCount].cookie, cookie, CookieLen);
    g_receivedJobs[g_receivedJobCount].source = sourceId;
    g_receivedJobs[g_receivedJobCount].state  = kStateRunning;
    g_receivedJobs[g_receivedJobCount].job    = theJob;
    if (theJob.type == kJobCSource)
    {
        char *source = malloc(strlen(theJob.cSource.source) + 1);
        if (!source)
        {
            return kNoMemory;
        } 
        strcpy(source, theJob.cSource.source);
        g_receivedJobs[g_receivedJobCount].job.cSource.source = source;
    }
    ++g_receivedJobCount;
    return kSuccess;
}

internal void
ExecuteNextJob(void)
{
    int idx = (int)g_receivedJobCount - 1;
    if (idx >= 0)
    {
        WriteToLog("Running job: %s\n", CookieToTemporaryString(g_receivedJobs[idx].cookie));

        int result = RunCode(g_receivedJobs[idx].cookie, g_receivedJobs[idx].job.cSource.source);
        WriteToLog("Result: %s\n", ErrorToString(result));

        g_receivedJobs[idx].state = kStateFinished;
        --g_receivedJobCount;
    }
}
