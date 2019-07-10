#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

#include "p2pjs.h"


typedef struct user_command
{
    int type;
    union {
        struct
        {
            char *path;
            double arg;
        } cSource;
    };

    struct user_command *next; 
} user_command;

global_variable pthread_mutex_t g_commandLock = PTHREAD_MUTEX_INITIALIZER;
global_variable pthread_t g_uiThread;

global_variable user_command *g_userCommandList;

internal void* UIThread(void*);

internal user_command*
GetNextCommand(void)
{
    user_command *cmd = 0;
    pthread_mutex_lock(&g_commandLock);
    cmd = g_userCommandList;
    if (g_userCommandList)
        g_userCommandList = g_userCommandList->next;  
    pthread_mutex_unlock(&g_commandLock);
    return cmd;
}

internal void
FreeCommand(user_command *cmd)
{
    if (cmd->type == kCmdJobCSource)
    {
        free(cmd->cSource.path);
    }
    free(cmd);
}

internal int 
StartUIThread(void)
{
    int r = pthread_create(&g_uiThread, 0, UIThread, 0);
    if (r == 0)
        return kSuccess;
    return kSyscallFailed;
}

internal void 
StopUIThread(void)
{
    pthread_kill(g_uiThread, SIGTERM);
}

internal void*
UIThread(void* _threadParam)
{
    Unused(_threadParam);

    while (1)
    {
        // BAD_DESIGN(Kevin): This is super simplistic
        char command[80];
        scanf("%s", command);

        if (strcmp(command, "job") == 0)
        {
            char path[240];
            scanf("%s", path);
            double arg;
            scanf("%lf", &arg);
            pthread_mutex_lock(&g_commandLock);
            user_command *cmd = malloc(sizeof(user_command));
            if (cmd)
            {
                cmd->type = kCmdJobCSource;
                cmd->cSource.path = malloc(strlen(path) + 1);
                if (cmd->cSource.path)
                {
                    strcpy(cmd->cSource.path, path);
                    cmd->cSource.arg = arg;
                    cmd->next = g_userCommandList;
                    g_userCommandList = cmd;
                }
                else
                {
                    free(cmd);
                }
            } 
            pthread_mutex_unlock(&g_commandLock);
        }
        else if (strcmp(command, "quit") == 0)
        {
            pthread_mutex_lock(&g_commandLock);
            user_command *cmd = malloc(sizeof(user_command));
            if (cmd)
            {
                cmd->type = kCmdQuit;
                cmd->next = g_userCommandList;
                g_userCommandList = cmd;
            } 
            pthread_mutex_unlock(&g_commandLock);
        }
        else
        {
            printf("Unknown command %s\n", command); 
        }
    } 
    printf("UI Thread Exited!\n");

    return 0;
}
