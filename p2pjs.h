#ifndef P2PJS_H
#define P2PJS_H

// NOTE(Kevin): Because static can mean a _lot_ of different things in C++
// [not so much in C, but i'v gotten used to it]
#define global_variable static
#define local_persist   static
#define internal        static

#define SizeofArray(A)  (sizeof((A))/sizeof((A)[0]))
#define Unused(V)       ((void)sizeof((V)))

#ifndef P2PJS_MaxRunningJobs
  #define P2PJS_MaxRunningJobs 4
#endif

// NOTE(Kevin): LLP64; should be fine under Windows and most *nix
typedef unsigned char       uint8;
typedef unsigned short      uint16;
typedef unsigned int        uint32;
typedef unsigned long long  uint64;
typedef char                int8;
typedef short               int16;
typedef int                 int32;
typedef long long           int64;
typedef int32               bool32;

// FIXME(Kevin): These types all assume that byte-ordering is not an issue.
// Which is of course horribly naive, but i don't got time for fixing this.

#define MaxConnectedPeers 8 

// NOTE(Kevin): 8 groups of 4 hex-digits, separated by (7) colons + 1 zero byte
#define PeerIPLen   (8*4+7+1)
// 65535
#define PeerPortLen 6
// NOTE(Kevin): Information about a peer that gets shared between peers
typedef struct 
{ 
    char ipaddr[PeerIPLen];
    char port[PeerPortLen];
} peer_info;

// Message Types
enum 
{
    // NOTE(Kevin): Sent when connecting to a new peer,
    // contains information about how to connect to me
    kHello,

    // NOTE(Kevin): Asks for a list of all known peers
    kGetPeers,

    // NOTE(Kevin): List of known peers
    kPeerList,

    // NOTE(Kevin): Ask for job resources
    kQueryJobResources,

    // NOTE(Kevin): Reply to queryJobResources
    kOfferJobResources,

    // NOTE(Kevin): Transmit a job
    kJob,

    kJobResult,
};

// Job flags
enum
{
    // NOTE(Kevin): The job is given as c source code
    kJobCSource = 0x00000001,

    // TODO(Kevin): Others?
};

// NOTE(Kevin): SHA-256 Hashes are 32 byte
#define CookieLen 32

typedef struct 
{
    uint16 type; 
    union
    {
        struct
        {
            char port[PeerPortLen];
        } hello;

        // NOTE(Kevin): Get peers has no data
        
        struct
        {
            uint16 numberOfPeers;
            peer_info peers[1];
        } peerList;

        struct
        {
            uint32      type; 
            uint8       cookie[CookieLen];
            peer_info   source;
        } queryJobResources;

        struct
        {
            uint8 cookie[CookieLen];
        } offerJobResources;

        struct
        {
            uint8 cookie[CookieLen];
            uint32 type;
            union
            {
                struct
                {
                    uint32 sourceLen;
                    char   *source;
                } cSource;
            };
        } job;
    }; 
} message;

// Errors
enum
{
    // NOTE(Kevin): Everything OK
    kSuccess,

    // NOTE(Kevin): A call to malloc etc. failed
    kNoMemory,

    // NOTE(Kevin): A system call returned an error
    kSyscallFailed,

    // NOTE(Kevin): A call would block, but is non-blocking
    kWouldBlock,

    kInvalidValue,

    kNoAvailableInterface,
    
    kCouldNotOpenFile,

    kJobNotFound,
};

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
        "JobNotFound"
    }; 
    return strings[error];
}

typedef struct
{
    int fd;
    int id;
} peer_iterator;

typedef struct
{
    uint32 type;
    union
    {
        struct
        {
            const char *source;
        } cSource;
    };
} job;

#endif
