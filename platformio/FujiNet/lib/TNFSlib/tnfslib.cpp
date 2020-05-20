#include <string.h>
#include "tnfslib.h"
#include "../tcpip/fnUDP.h"
#include "../utils/utils.h"
#include "../hardware/fnSystem.h"

bool _tnfs_transaction(tnfsMountInfo &m_info, tnfsPacket &pkt, uint16_t datalen);

const char *_tnfs_result_code_string(int resultcode);
void _tnfs_debug_packet(const tnfsPacket &pkt, unsigned short len, bool isResponse = false);

/*
MOUNT - Command ID 0x00
-----------------------

Format:
Standard header followed by:
Bytes 4+: 16 bit version number, little endian, LSB = minor, MSB = major
          NULL terminated string: mount location
          NULL terminated string: user id (optional - NULL if no user id)
          NULL terminated string: password (optional - NULL if no passwd)

Example:

To mount /home/tnfs on the server, with user id "example" and password of
"password", using version 1.2 of the protocol:
0x0000 0x00 0x00 0x02 0x01 /home/tnfs 0x00 example 0x00 password 0x00

To mount "a:" anonymously, using version 1.2 of the protocol:
0x0000 0x00 0x00 0x02 0x01 a: 0x00 0x00 0x00

The server responds with the standard header. If the operation was successful,
the standard header contains the session number, and the TNFS protocol
version that the server is using following the header, followed by the
minimum retry time in milliseconds as a little-endian 16 bit number.
Clients must respect this minimum retry value, especially for a server
with a slow underlying file system such as a floppy disc, to avoid swamping
the server. A client should also never have more than one request "in flight"
at any one time for any operation where order is important, so for example,
if reading a file, don't send a new request to read from a given file handle
before completing the last request.

Example: A successful MOUNT command was carried out, with a server that
supports version 2.6, and has a minimum retry time of 5 seconds (5000 ms,
hex 0x1388). Session ID is 0xBEEF:

0xBEEF 0x00 0x00 0x00 0x06 0x02 0x88 0x13

Example: A failed MOUNT command with error 1F for a version 3.5 server:
0x0000 0x00 0x00 0x1F 0x05 0x03
*/
/* Logs-in to the TNFS server by providing a mount path, user and password.
 Success will result in a session ID set in tnfsMountInfo.
 If the host_ip is set, it will be used in all transactions instead of hostname.
 Currently, mountpath, userid and password are ignored.
 port, timeout_ms, and max_retries may be set or left to defaults.
*/
bool tnfs_mount(tnfsMountInfo &m_info)
{
    // Unmount if we happen to have sesssion
    if (m_info.session != 0)
        tnfs_umount(m_info);
    m_info.session = 0; // In case tnfs_umount fails - throw out the current session ID

    tnfsPacket packet;
    packet.command = TNFS_CMD_MOUNT;

    // TNFS VERSION
    packet.payload[0] = 0x00; // TNFS Version Minor (LSB)
    packet.payload[1] = 0x01; // TNFS Version Major (MSB)
    // TODO: Copy mountpath while observing max packet size
    packet.payload[2] = 0x2F; // '/'
    packet.payload[3] = 0x00; // nul
    // TODO: Copy username while observing max packet size
    // TODO: Copy password while observing max packet size
    packet.payload[4] = 0x00;
    packet.payload[5] = 0x00;

    if (_tnfs_transaction(m_info, packet, 6))
    {
        // Zero on success
        if (packet.payload[0] == 0)
        {
            m_info.session = TNFS_UINT16_FROM_HILOBYTES(packet.session_idh, packet.session_idl);
            m_info.server_version = TNFS_UINT16_FROM_HILOBYTES(packet.payload[2], packet.payload[1]);
            m_info.min_retry_ms = TNFS_UINT16_FROM_HILOBYTES(packet.payload[4], packet.payload[3]);
            return true;
        }
    }
    return false;
}

/*
UMOUNT - Command ID 0x01
------------------------

Format:
Standard header only, containing the connection ID to terminate, 0x00 as
the sequence number, and 0x01 as the command.

Example:
To UMOUNT the filesystem mounted with id 0xBEEF:

0xBEEF 0x00 0x01

The server responds with the standard header and a return code as byte 4.
The return code is 0x00 for OK. Example:

0xBEEF 0x00 0x01 0x00

On error, byte 4 is set to the error code, for example, for error 0x1F:

0xBEEF 0x00 0x01 0x1F
*/
/* Logs off TNFS server given data (session, host) in tnfsMountInfo
*/
bool tnfs_umount(tnfsMountInfo &m_info)
{

    tnfsPacket packet;
    packet.command = TNFS_CMD_UNMOUNT;

    if (_tnfs_transaction(m_info, packet, 0))
    {
        m_info.session = 0;
        return true;
    }
    return false;
}

/*
OPENDIR - Open a directory for reading - Command ID 0x10
--------------------------------------------------------

Format:
Standard header followed by a null terminated absolute path.
The path delimiter is always a "/". Servers whose underlying 
file system uses other delimiters, such as Acorn ADFS, should 
translate. Note that any recent version of Windows understands "/" 
to be a path delimiter, so a Windows server does not need
to translate a "/" to a "\".
Clients should keep track of their own current working directory.

Example:
0xBEEF 0x00 0x10 /home/tnfs 0x00 - Open absolute path "/home/tnfs"

The server responds with the standard header, with byte 4 set to the
return code which is 0x00 for success, and if successful, byte 5 
is set to the directory handle.

Example:
0xBEEF 0x00 0x10 0x00 0x04 - Successful, handle is 0x04
0xBEEF 0x00 0x10 0x1F - Failed with code 0x1F
*/
/*
    Opens directory and stores directory handle in tnfsMountInfo.dir_handle
*/
bool tnfs_opendir(tnfsMountInfo &m_info, const char *directory)
{
    if (directory == nullptr)
        return false;

    tnfsPacket packet;
    packet.command = TNFS_CMD_OPENDIR;

    // Make sure we start with a '/'
    if (directory[0] != '/')
    {
        packet.payload[0] = '/';
        strncpy((char *)&packet.payload[1], directory, sizeof(packet.payload) - 1);
    }
    else
    {
        strncpy((char *)&packet.payload[0], directory, sizeof(packet.payload));
    }

#ifdef DEBUG
    Debug_printf("TNFS open directory: \"%s\"\n", (char *)packet.payload);
#endif

    int len = strlen((char *)packet.payload) + 1;
    if (_tnfs_transaction(m_info, packet, len))
    {
        m_info.dir_handle = packet.payload[1];
#ifdef DEBUG
        Debug_printf("Directory opened, handle ID: %hhd\n", m_info.dir_handle);
#endif
        return true;
    }
    return false;
}

/*
READDIR - Reads a directory entry - Command ID 0x11
---------------------------------------------------

Format:
Standard header plus directory handle.

Example:
0xBEEF 0x00 0x11 0x04 - Read an entry with directory handle 0x04

The server responds with the standard header, followed by the directory
entry. Example:

0xBEEF 0x17 0x11 0x00 . 0x00 - Directory entry for the current working directory
0xBEEF 0x18 0x11 0x00 .. 0x00 - Directory entry for parent
0xBEEF 0x19 0x11 0x00 foo 0x00 - File named "foo"

If the end of directory is reached, or another error occurs, then the
status byte is set to the error number as for other commands.
0xBEEF 0x1A 0x11 0x21 - EOF
0xBEEF 0x1B 0x11 0x1F - Error code 0x1F

*/
/*
    Reads next available file using open directory handle specified in
    tnfsMountInfo.dir_handle
*/
bool tnfs_readdir(tnfsMountInfo &m_info, char *dir_entry, int dir_entry_len)
{
    // Check for a valid open handle ID
    if(m_info.dir_handle < 0 || m_info.dir_handle > 255)
        return false;

    tnfsPacket packet;
    packet.command = TNFS_CMD_READDIR;
    packet.payload[0] = m_info.dir_handle;

    if (_tnfs_transaction(m_info, packet, 1))
    {
        if (packet.payload[0] == TNFS_RESULT_SUCCESS)
        {
            strncpy(dir_entry, (char *)&packet.payload[1], dir_entry_len);
            return true;
        }
    }
    return false;
}

/*
CLOSEDIR - Close a directory handle - Command ID 0x12
-----------------------------------------------------

Format:
Standard header plus directory handle.

Example, closing handle 0x04:
0xBEEF 0x00 0x12 0x04

The server responds with the standard header, with byte 4 set to the
return code which is 0x00 for success, or something else for an error.
Example:
0xBEEF 0x00 0x12 0x00 - Close operation succeeded.
0xBEEF 0x00 0x12 0x1F - Close failed with error code 0x1F
*/
/*
    Closes current directory handle specificed in tnfsMountInfo
*/
bool tnfs_closedir(tnfsMountInfo &m_info)
{
    tnfsPacket packet;
    packet.command = TNFS_CMD_CLOSEDIR;
    packet.payload[0] = m_info.dir_handle;

    if (_tnfs_transaction(m_info, packet, 1))
    {
        if (packet.payload[0] == 0)
        {
            m_info.dir_handle = 0;
            return true;
        }
        else
        {
#ifdef DEBUG
            Debug_printf("TNFS close dir failed with code %hhu \"%s\"\n", packet.payload[0], _tnfs_result_code_string(packet.payload[0]));
#endif
        }
    }
    return false;
}

/*
MKDIR - Make a new directory - Command ID 0x13
----------------------------------------------

Format:
Standard header plus a null-terminated absolute path.

Example:
0xBEEF 0x00 0x13 /foo/bar/baz 0x00

The server responds with the standard header plus the return code:
0xBEEF 0x00 0x13 0x00 - Directory created successfully
0xBEEF 0x00 0x13 0x02 - Directory creation failed with error 0x02
*/
/*
    Creates directory.
    Return 0 on success.
    Returns -1 on command transmission failure.
    Otherwise returns server response code
*/
int tnfs_mkdir(tnfsMountInfo &m_info, const char *directory)
{
    if (directory == nullptr)
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_MKDIR;

    // Make sure we start with a '/'
    if (directory[0] != '/')
    {
        packet.payload[0] = '/';
        strncpy((char *)&packet.payload[1], directory, sizeof(packet.payload) - 1);
    }
    else
    {
        strncpy((char *)&packet.payload[0], directory, sizeof(packet.payload));
    }

#ifdef DEBUG
    Debug_printf("TNFS make directory: \"%s\"\n", (char *)packet.payload);
#endif

    int len = strlen((char *)packet.payload) + 1;
    if (_tnfs_transaction(m_info, packet, len))
    {
        return packet.payload[0];
    }
    return -1;
}

/*
RMDIR - Remove a directory - Command ID 0x14
--------------------------------------------

Format:
Standard header plus a null-terminated absolute path.

Example:
0xBEEF 0x00 0x14 /foo/bar/baz 0x00

The server responds with the standard header plus the return code:
0xBEEF 0x00 0x14 0x00 - Directory was deleted.
0xBEEF 0x00 0x14 0x02 - Directory delete operation failed with error 0x02
*/
/*
    Deletes directory.
    Return 0 on success.
    Returns -1 on command transmission failure.
    Otherwise returns server response code
*/
int tnfs_rmdir(tnfsMountInfo &m_info, const char *directory)
{
    if (directory == nullptr)
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_RMDIR;

    // Make sure we start with a '/'
    if (directory[0] != '/')
    {
        packet.payload[0] = '/';
        strncpy((char *)&packet.payload[1], directory, sizeof(packet.payload) - 1);
    }
    else
    {
        strncpy((char *)&packet.payload[0], directory, sizeof(packet.payload));
    }

#ifdef DEBUG
    Debug_printf("TNFS remove directory: \"%s\"\n", (char *)packet.payload);
#endif

    int len = strlen((char *)packet.payload) + 1;
    if (_tnfs_transaction(m_info, packet, len))
    {
        return packet.payload[0];
    }
    return -1;
}


/*
STAT - Get information on a file - Command 0x24
-----------------------------------------------
The request consists of the standard header, followed by the full path
of the file to stat, terminated by a NULL. Example:

0xBEEF 0x00 0x24 /foo/bar/baz.txt 0x00

The server replies with the standard header, followed by the return code.
On success, the file information follows this. Stat information is returned
in this order. Not all values are used by all servers. At least file
mode and size must be set to a valid value (many programs depend on these).

File mode	- 2 bytes: file permissions - little endian byte order
uid		- 2 bytes: Numeric UID of owner
gid		- 2 bytes: Numeric GID of owner
size		- 4 bytes: Unsigned 32 bit little endian size of file in bytes
atime		- 4 bytes: Access time in seconds since the epoch, little end.
mtime		- 4 bytes: Modification time in seconds since the epoch,
                           little endian
ctime		- 4 bytes: Time of last status change, as above.
uidstring	- 0 or more bytes: Null terminated user id string
gidstring	- 0 or more bytes: Null terminated group id string

Fields that don't apply to the server in question should be left as 0x00.
The ´mtime' field and 'size' fields are unsigned 32 bit integers.
The uidstring and gidstring are helper fields so the client doesn't have
to then ask the server for the string representing the uid and gid.

File mode flags will be most useful for code that is showing a directory
listing, and for programs that need to find out what kind of file (regular
file or directory, etc) a particular file may be. They follow the POSIX
convention which is:

Flags		Octal representation
S_IFREG		0100000		Is a regular file
S_IFDIR		0040000		Directory
*/
/*
    Returns file information filled in tnfsStat.
    Return 0 on success.
    Returns -1 on command transmission failure.
    Otherwise returns server response code
*/
int tnfs_stat(tnfsMountInfo &m_info, tnfsStat &filestat, const char *filepath)
{
#define OFFSET_FILEMODE 1
#define OFFSET_UID 3
#define OFFSET_GID 5
#define OFFSET_FILESIZE 7
#define OFFSET_ATIME 11
#define OFFSET_MTIME 15
#define OFFSET_CTIME 19

    if (filepath == nullptr)
        return -1;

    tnfsPacket packet;
    packet.command = TNFS_CMD_STAT;

    // Make sure we start with a '/'
    if (filepath[0] != '/')
    {
        packet.payload[0] = '/';
        strncpy((char *)&packet.payload[1], filepath, sizeof(packet.payload) - 1);
    }
    else
    {
        strncpy((char *)&packet.payload[0], filepath, sizeof(packet.payload));
    }

#ifdef DEBUG
    Debug_printf("TNFS stat: \"%s\"\n", (char *)packet.payload);
#endif

    int len = strlen((char *)packet.payload) + 1;
    if (_tnfs_transaction(m_info, packet, len))
    {
        uint16_t filemode = TNFS_UINT16_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_FILEMODE);
        filestat.isDir = (filemode & S_IFDIR) ? true : false;

        filestat.filesize = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_FILESIZE);

        filestat.a_time = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_ATIME);
        filestat.m_time = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_MTIME);
        filestat.c_time = TNFS_UINT32_FROM_LOHI_BYTEPTR(packet.payload + OFFSET_CTIME);

#ifdef DEBUG
    Debug_printf("\tdir: %d, size: %u, atime: 0x%04x, mtime: 0x%04x, ctime: 0x%04x\n", filestat.isDir ? 1 : 0,
        filestat.filesize, filestat.a_time, filestat.m_time, filestat.c_time );
#endif
        return packet.payload[0];
    }
    return -1;
}

/*
  Send constructed TNFS packet and check for reply
  The send/receive loop will be attempted tnfsPacket.max_retries times (default: TNFS_RETRIES)
  Each retry attempt is limited to tnfsPacket.timeout_ms (default: TNFS_TIMEOUT)

  Only the command (tnfsPacket.command) and payload contents need to be set on the packet.
  Current session ID will be copied from tnfsMountInfo and retryCount is always reset to zero.
  
  If successful, server's response code will be the first byte of of tnfsPacket.data
  
  returns - true if response packet was received
            false if no response received during retries/timeout period
 */
bool _tnfs_transaction(tnfsMountInfo &m_info, tnfsPacket &pkt, uint16_t payload_size)
{
    fnUDP udp;

    // Set our session ID
    pkt.session_idl = TNFS_LOBYTE_FROM_UINT16(m_info.session);
    pkt.session_idh = TNFS_HIBYTE_FROM_UINT16(m_info.session);

    // Start a new retry sequence
    int retry = 0;
    while (retry < m_info.max_retries)
    {
        // Set the sequence number
        pkt.sequence_num = m_info.current_sequence_num++;

#ifdef DEBUG
        _tnfs_debug_packet(pkt, payload_size);
#endif

        // Send packet
        bool sent = false;
        // Use the IP address if we have it
        if (m_info.host_ip != IPADDR_NONE)
            sent = udp.beginPacket(m_info.host_ip, m_info.port);
        else
            sent = udp.beginPacket(m_info.hostname, m_info.port);

        if (sent)
        {
            udp.write(pkt.rawData, payload_size + TNFS_HEADER_SIZE); // Add the data payload along with 4 bytes of TNFS header
            sent = udp.endPacket();
        }

        if (!sent)
        {
#ifdef DEBUG
            Debug_println("Failed to send packet - retrying");
#endif
        }
        else
        {
            // Wait for a response at most TNFS_TIMEOUT milliseconds
            int ms_start = fnSystem.millis();
            uint8_t current_sequence_num = pkt.sequence_num;
            do
            {
                if (udp.parsePacket())
                {
                    __BEGIN_IGNORE_UNUSEDVARS
                    unsigned short l = udp.read(pkt.rawData, sizeof(pkt.rawData));
                    __END_IGNORE_UNUSEDVARS
#ifdef DEBUG
                    _tnfs_debug_packet(pkt, l, true);
#endif

                    // Out of order packet received.
                    if (pkt.sequence_num != current_sequence_num)
                    {
#ifdef DEBUG
                        Debug_println("TNFS OUT OF ORDER SEQUENCE! RETRYING");
#endif
                        // Fall through and let retry logic handle it.
                    }
                    else
                        return true;
                }
                fnSystem.yield();

            } while ((fnSystem.millis() - ms_start) < m_info.timeout_ms);

#ifdef DEBUG
            Debug_printf("Timeout after %d milliseconds. Retrying\n", m_info.timeout_ms);
#endif
        }

        // Make sure we wait before retrying
        vTaskDelay(m_info.min_retry_ms / portTICK_PERIOD_MS);
        retry++;
    }

#ifdef DEBUG
    Debug_println("Retry attempts failed");
#endif
    return false;
}

/*
  Dump TNFS packet to debug
  unsigned short len - packet data payload length
  bool isResponse - parse result code
*/
void _tnfs_debug_packet(const tnfsPacket &pkt, unsigned short payload_size, bool isResponse)
{
#ifdef TNFS_DEBUG_VERBOSE
    // Remove header bytes from count of response packets since we only care about the count of the data payload
    if (isResponse)
    {
        payload_size -= TNFS_HEADER_SIZE;
        Debug_printf("TNFS << RX packet, len: %d, response (%hhu): %s\n", payload_size, pkt.payload[0], _tnfs_result_code_string((int)pkt.payload[0]));
    }
    else
        Debug_printf("TNFS >> TX packet, len: %d\n", payload_size);

    Debug_printf("\t[%02x%02x %02x %02x] ", pkt.session_idh, pkt.session_idl, pkt.sequence_num, pkt.command);
    for (int i = 0; i < payload_size; i++)
        Debug_printf("%02x ", pkt.payload[i]);
    Debug_println();
#endif
}

const char *_tnfs_result_code_string(int resultcode)
{
#ifdef TNFS_DEBUG_VERBOSE
    switch (resultcode)
    {
    case TNFS_RESULT_SUCCESS:
        return "Success";
    case TNFS_RESULT_NOT_PERMITTED:
        return "EPERM: Operation not permitted";
    case TNFS_RESULT_FILE_NOT_FOUND:
        return "ENOENT: No such file or directory";
    case TNFS_RESULT_IO_ERROR:
        return "EIO: I/O error";
    case TNFS_RESULT_NO_SUCH_DEVICE:
        return "ENXIO: No such device or address";
    case TNFS_RESULT_LIST_TOO_LONG:
        return "E2BIG: Argument list too long";
    case TNFS_RESULT_BAD_FILENUM:
        return "EBADF: Bad file number";
    case TNFS_RESULT_TRY_AGAIN:
        return "EAGAIN: Try again";
    case TNFS_RESULT_OUT_OF_MEMORY:
        return "ENOMEM: Out of memory";
    case TNFS_RESULT_ACCESS_DENIED:
        return "EACCES: Permission denied";
    case TNFS_RESULT_RESOURCE_BUSY:
        return "EBUSY: Device or resource busy";
    case TNFS_RESULT_FILE_EXISTS:
        return "EEXIST: File exists";
    case TNFS_RESULT_NOT_A_DIRECTORY:
        return "ENOTDIR: Is not a directory";
    case TNFS_RESULT_IS_DIRECTORY:
        return "EISDIR: Is a directory";
    case TNFS_RESULT_INVALID_ARGUMENT:
        return "EINVAL: Invalid argument";
    case TNFS_RESULT_FILE_TABLE_OVERFLOW:
        return "ENFILE: File table overflow";
    case TNFS_RESULT_TOO_MANY_FILES_OPEN:
        return "EMFILE: Too many open files";
    case TNFS_RESULT_FILE_TOO_LARGE:
        return "EFBIG: File too large";
    case TNFS_RESULT_NO_SPACE_ON_DEVICE:
        return "ENOSPC: No space left on device";
    case TNFS_RESULT_CANNOT_SEEK_PIPE:
        return "ESPIPE: Attempt to seek on a FIFO or pipe";
    case TNFS_RESULT_READONLY_FILESYSTEM:
        return "EROFS: Read only filesystem";
    case TNFS_RESULT_NAME_TOO_LONG:
        return "ENAMETOOLONG: Filename too long";
    case TNFS_RESULT_FUNCTION_UNIMPLEMENTED:
        return "ENOSYS: Function not implemented";
    case TNFS_RESULT_DIRECTORY_NOT_EMPTY:
        return "ENOTEMPTY: Directory not empty";
    case TNFS_RESULT_TOO_MANY_SYMLINKS:
        return "ELOOP: Too many symbolic links";
    case TNFS_RESULT_NO_DATA_AVAILABLE:
        return "ENODATA: No data available";
    case TNFS_RESULT_OUT_OF_STREAMS:
        return "ENOSTR: Out of streams resources";
    case TNFS_RESULT_PROTOCOL_ERROR:
        return "EPROTO: Protocol error";
    case TNFS_RESULT_BAD_FILE_DESCRIPTOR:
        return "EBADFD: File descriptor in bad state";
    case TNFS_RESULT_TOO_MANY_USERS:
        return "EUSERS: Too many users";
    case TNFS_RESULT_OUT_OF_BUFFER_SPACE:
        return "ENOBUFS: No buffer space avaialable";
    case TNFS_RESULT_ALREADY_IN_PROGRESS:
        return "EALREADY: Operation already in progress";
    case TNFS_RESULT_STALE_HANDLE:
        return "ESTALE: Stale TNFS handle";
    case TNFS_RESULT_END_OF_FILE:
        return "EOF: End of file";
    case TNFS_RESULT_INVALID_HANDLE:
        return "Invalid TNFS handle";
    default:
        return "Unknown result code";
    }
#else
    return nullptr;
#endif
}
