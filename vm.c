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

typedef struct
{
    uint8 cookie[CookieLen];
    int isValid;
    double arg;
} job_data;


internal int 
EmitCSourceJob(const char *sourcePath,
               double arg,
               const char *myIp, const char *myPort,
               uint8 cookieOut[CookieLen]);
internal int IsJobFinished(uint8 cookie[CookieLen]);
internal double GetJobResult(uint8 cookie[CookieLen]);
internal int GetNumberOfOutstandingJobs(void);

internal void Frame(void);

internal void
AllocateJob(WrenVM *vm)
{
    job_data *job = wrenSetSlotNewForeign(vm, 0, 0, sizeof(job_data));
    const char *path = wrenGetSlotString(vm, 1);
    double     arg   = wrenGetSlotDouble(vm, 2);
    job->isValid = EmitCSourceJob(path, arg, g_localIp, g_localPort, job->cookie) == kSuccess;
    job->arg = arg;

    Frame();
}

internal void
FinalizeJob(void *data)
{
    Unused(data);
    Frame();
}


internal void
JobIsFinished(WrenVM *vm)
{
    job_data *job = wrenGetSlotForeign(vm, 0);
    if (job->isValid)
    {
        wrenSetSlotBool(vm, 0, IsJobFinished(job->cookie)); 
    }
    else
    {
        wrenSetSlotString(vm, 0, "Job is not valid.");
        wrenAbortFiber(vm, 0);
    }
    Frame();
}

internal void
JobGetArgument(WrenVM *vm)
{
    job_data *job = wrenGetSlotForeign(vm, 0);
    if (job->isValid)
    {
        wrenSetSlotDouble(vm, 0, job->arg); 
    }
    else
    {
        wrenSetSlotString(vm, 0, "Job is not valid.");
        wrenAbortFiber(vm, 0);
    }
    Frame();
}

internal void
JobGetResult(WrenVM *vm)
{
    job_data *job = wrenGetSlotForeign(vm, 0);
    if (job->isValid)
    {
        wrenSetSlotDouble(vm, 0, GetJobResult(job->cookie)); 
    }
    else
    {
        wrenSetSlotString(vm, 0, "Job is not valid.");
        wrenAbortFiber(vm, 0);
    }
    Frame();
}

internal void
InterfaceGetNumberOfOutstandingJobs(WrenVM *vm)
{
    Frame();
    wrenSetSlotDouble(vm, 0, (double)GetNumberOfOutstandingJobs());
}

internal void
InterfaceIdle(WrenVM *vm)
{
    Unused(vm);
    Frame();
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
CodeOutput(WrenVM *vm, const char *text)
{
    Unused(vm);
    WriteToLog("[%s] %s", CookieToTemporaryString(g_currentCookie), text);
}

internal void
CodeError(WrenVM* vm, 
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
    if (strcmp(module, "p2pjs") == 0)
    {
        if (strcmp(class, "Job") == 0)
        {
            if (!isStatic && strcmp(signature, "isFinished()") == 0)
            {
                return JobIsFinished;
            }
            else if (!isStatic && strcmp(signature, "getResult()") == 0)
            {
                return JobGetResult;
            }
            else if (!isStatic && strcmp(signature, "getArgument()") == 0)
            {
                return JobGetArgument;
            }
        }
        else if (strcmp(class, "Interface") == 0)
        {
            if (isStatic && strcmp(signature, "getNumberOfOutstandingJobs()") == 0)
            {
                return InterfaceGetNumberOfOutstandingJobs;
            }
            else if (isStatic && strcmp(signature, "idle()") == 0)
            {
                return InterfaceIdle;
            }
        }
    }
    return 0;
}

internal WrenForeignClassMethods
BindForeignClass(WrenVM* vm, const char* module, const char* className)
{
    Unused(vm);
    WrenForeignClassMethods methods;
    memset(&methods, 0, sizeof(methods));
    if (strcmp(module, "p2pjs") == 0)
    {
        if (strcmp(className, "Job") == 0)
        {
            methods.allocate = AllocateJob;
            methods.finalize = FinalizeJob;
        }
    }
    return methods;
}

internal int 
RunCode(uint8 cookie[CookieLen], double arg, const char *source)
{
    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.loadModuleFn = LoadModule;
    config.writeFn      = CodeOutput;
    config.errorFn      = CodeError;
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

internal void
ScriptOutput(WrenVM *vm, const char *text)
{
    Unused(vm);
    WriteToUser("%s", text);
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
        WriteToUser("[main] In module %s, line %d: %s\n",
                module,
                line,
                message);
    }
    else if (type == WREN_ERROR_RUNTIME)
    {
        WriteToUser("[main] Runtime error: %s\n",
                message);
    }
    else
    {
        WriteToUser("   %s:%d %s\n", module, line, message);
    }
}

internal int
RunScript(const char *path)
{
    FILE *file = fopen(path, "r");
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
        int c = fgetc(file);
        if (c != EOF)
        {
            source[sourceLength++] = (char)c; 
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
                memset(&t[sourceCapacity], 0, sourceCapacity);
                sourceCapacity *= 2;
            }
        }
    }
    source[sourceLength] = '\0';
    fclose(file);

    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.loadModuleFn = LoadModule;
    config.writeFn      = ScriptOutput;
    config.errorFn      = ScriptError;
    config.bindForeignMethodFn = BindForeignMethod;
    config.bindForeignClassFn  = BindForeignClass;

    WrenVM *vm = wrenNewVM(&config);
    WrenInterpretResult result = wrenInterpret(vm, "main", source);
    wrenFreeVM(vm);
    if (result == WREN_RESULT_COMPILE_ERROR)
        return kCompileError;
    else if (result == WREN_RESULT_RUNTIME_ERROR)
        return kRuntimeError;
    else
        return kSuccess;
}
