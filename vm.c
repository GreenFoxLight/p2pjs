#include <wren.h>
#include <stdio.h>

#include "p2pjs.h"

global_variable uint8 g_currentCookie[CookieLen];
global_variable double g_jobResult;

internal double
GetLastResult(void)
{
    return g_jobResult;
}

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
        int c = fgetc(file);
        if (c != EOF)
        {
            source[sourceLength++] = (char)c; 
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
        }
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

internal WrenForeignMethodFn
BindForeignMethod(WrenVM *vm,
                  const char *module,
                  const char *class,
                  bool isStatic,
                  const char *signature)
{
    Unused(vm);
    Unused(isStatic);
    return 0;
}

internal int 
RunCode(uint8 cookie[CookieLen], double arg, const char *source)
{
    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.loadModuleFn = LoadModule;
    config.writeFn      = ScriptOutput;
    config.errorFn      = ScriptError;
    config.bindForeignMethodFn = BindForeignMethod;

    WrenVM *vm = wrenNewVM(&config);
    memcpy(g_currentCookie, cookie, CookieLen);
    WrenInterpretResult result = wrenInterpret(vm,
                                               CookieToTemporaryString(cookie), 
                                               source);
    if (result == WREN_RESULT_COMPILE_ERROR)
        return kCompileError;
    else if (result == WREN_RESULT_RUNTIME_ERROR)
        return kRuntimeError;

    WrenHandle *runSignature = wrenMakeCallHandle(vm, "run(_)");

    wrenEnsureSlots(vm, 1);
    wrenGetVariable(vm, CookieToTemporaryString(cookie), "Job", 0);
    WrenHandle *jobClass = wrenGetSlotHandle(vm, 0);

    wrenEnsureSlots(vm, 2);
    wrenSetSlotHandle(vm, 0, jobClass);
    wrenSetSlotDouble(vm, 1, arg);
    result = wrenCall(vm, runSignature);
    g_jobResult = wrenGetSlotDouble(vm, 0);

    wrenReleaseHandle(vm, jobClass);
    wrenReleaseHandle(vm, runSignature);

    wrenFreeVM(vm);

    if (result == WREN_RESULT_SUCCESS)
        return kSuccess;
    else
        return kRuntimeError;
}
