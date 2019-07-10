#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "p2pjs.h"

#define LogFilenamePrefix "p2pjs_"
#define LogFilenameSuffix ".log"

#ifndef P2PJS_LOGTOSTDERR
  #define P2PJS_LOGTOSTDERR 1
#endif

global_variable FILE *g_logFile;

internal bool32 
OpenLog(void)
{
#if !P2PJS_LOGTOSTDERR
    time_t startTime = time(0);
    char *startTimeString = ctime(&startTime);
    char *fileName = malloc(strlen(LogFilenamePrefix) +
                            strlen(startTimeString) +
                            strlen(LogFilenameSuffix) + 1);
    if (!fileName)
        return 0;
    strcpy(fileName, LogFilenamePrefix);
    if (startTimeString[strlen(startTimeString)] < ' ')
        strncat(fileName, startTimeString, strlen(startTimeString) - 1);
    else
        strncat(fileName, startTimeString, strlen(startTimeString));
    strcat(fileName, LogFilenameSuffix);
    g_logFile = fopen(fileName, "w");
    if (!g_logFile) {
        fprintf(stderr, "Can not open log file %s\n", fileName);
        return 0;
    }
    free(fileName);
#endif
    return 1;
}

internal void
WriteToLog_(bool32 toUser, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    time_t currentTime = time(0);
    char *timeString = ctime(&currentTime);
    char *messageFormat = malloc(strlen(timeString) + strlen(fmt) + 3);
    if (!messageFormat)
    {
        if (g_logFile)
        {
            if (toUser)
            {
                vfprintf(stderr, fmt, ap);
                va_start(ap, fmt);
            }
            fprintf(g_logFile, "%s ", timeString);
            vfprintf(g_logFile, fmt, ap);
            fflush(g_logFile);
        }
        else
        {
            fprintf(stderr, "%s ", timeString);
            vfprintf(stderr, fmt, ap);
        }
    }
    else
    {
        strcpy(messageFormat, timeString); 
        if (timeString[strlen(timeString)] < ' ')
            messageFormat[strlen(timeString) - 1] = '\0';
        strcat(messageFormat, " ");
        strcat(messageFormat, fmt);
        if (g_logFile)
        {
            if (toUser)
            {
                vfprintf(stderr, fmt, ap);
                va_start(ap, fmt);
            }
            vfprintf(g_logFile, messageFormat, ap);
            fflush(g_logFile);
        }
        else
        {
            vfprintf(stderr, messageFormat, ap);
        }
        free(messageFormat);
    }
    va_end(ap);
}

#define WriteToLog(...) WriteToLog_(0, __VA_ARGS__)
#define WriteToUser(...) WriteToLog_(1, __VA_ARGS__)

internal void
CloseLog(void)
{
    if (g_logFile)
        fclose(g_logFile);
}
