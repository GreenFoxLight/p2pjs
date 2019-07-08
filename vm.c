#include <wren.h>
#include <stdio.h>

#include "p2pjs.h"

global_variable WrenVM *g_vm;
global_variable uint8 g_currentCookie[CookieLen];

internal char* 
LoadModule(WrenVM *vm, const char *name)
{
    Unused(vm);

    char *path = 0;
    unsigned int pathLen = strlen("modules/") +
                           strlen(name) +
                           strlen(".wren") +
                           1;
    path = malloc(pathLen);
    if (!path)
    {
        WriteToLog("Could not allocate memory for module path.\n");
        return 0;
    }
    strcpy(path, "modules/");
    strcat(path, name);
    strcat(path, ".wren");
    
    char *source = malloc(100);
    if (!source)
    {
        free(path);
        WriteToLog("Could not allocate memory for module source.\n");
        return 0;
    }
    unsigned int sourceLength = 0;
    unsigned int sourceCapacity = 100; 

    FILE *file = fopen(path, "r");
    while (!feof(file))
    {
        if (sourceLength == sourceCapacity)
        {
            char *t = realloc(source, 2 * sourceCapacity);
            if (!t)
            {
                free(source);
                free(path);
                return 0;
            }
            source = t;
            sourceCapacity *= 2;
        }
        source[sourceLength++] = fgetc(file);
    }
    source[sourceLength] = '\0';
    fclose(file);
    free(path);
    return source; 
}

internal const char* CookieToTemporaryString(uint8 cookie[CookieLen]);

internal void
ScriptOutput(WrenVM *vm, const char *text)
{
    Unused(vm);
    WriteToLog("[%s] %s", CookieToTemporaryString(g_currentCookie), text);
}

internal void
ScriptError(WrenVM* vm, 
            WrenErrorType type, 
            const char* module, 
            int line, 
            const char* message)
{
    Unused(vm);
    if (type == WREN_ERROR_COMPILE)
    {
        WriteToLog("[%s] In module %s, line %d: %s\n",
                CookieToTemporaryString(g_currentCookie),
                module,
                line,
                message);
    }
    else if (type == WREN_ERROR_RUNTIME)
    {
        WriteToLog("[%s] Runtime error: %s\n",
                CookieToTemporaryString(g_currentCookie),
                message);
    }
    else
    {
        WriteToLog("   %s:%d %s\n", module, line, message);
    }
}

internal int 
RunCode(uint8 cookie[CookieLen], const char *source)
{
    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.loadModuleFn = LoadModule;
    config.writeFn      = ScriptOutput;
    config.errorFn      = ScriptError;

    g_vm = wrenNewVM(&config);
    memcpy(g_currentCookie, cookie, CookieLen);
    // FIXME(Kevin): Figure out, where that weird extra byte comes from
    WrenInterpretResult result = wrenInterpret(g_vm,
                                               CookieToTemporaryString(cookie), 
                                               &source[1]);
    if (result == WREN_RESULT_COMPILE_ERROR)
        return kCompileError;
    else if (result == WREN_RESULT_RUNTIME_ERROR)
        return kRuntimeError;


    WrenHandle *runSignature = wrenMakeCallHandle(g_vm, "Run()");

    wrenEnsureSlots(g_vm, 1);
    wrenGetVariable(g_vm, CookieToTemporaryString(cookie), "job", 0);
    WrenHandle *jobClass = wrenGetSlotHandle(g_vm, 0);

    wrenSetSlotHandle(g_vm, 0, jobClass);
    result = wrenCall(g_vm, runSignature);

    wrenReleaseHandle(g_vm, jobClass);
    wrenReleaseHandle(g_vm, runSignature);

    wrenFreeVM(g_vm);

    if (result == WREN_RESULT_SUCCESS)
        return kSuccess;
    else
        return kRuntimeError;
}
