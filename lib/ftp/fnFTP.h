/**
 * FTP class for #FujiNet
 */

#ifndef FNFTP_H
#define FNFTP_H

#include <string>
#include "../tcpip/fnTcpClient.h"

using namespace std;

class fnFTP
{
public:

    /**
     * Log into FTP server.
     * @param username username for login
     * @param password password for login
     * @param hostname host to login
     * @param port port to login (default 21)
     * @return TRUE on error, FALSE on success
     */
    bool login(string _username, string _password, string _hostname, unsigned short _port = 21);

    /**
     * Log out of FTP server, closes control connection.
     * @return TRUE on error, FALSE on success.
     */
    bool logout();

    /**
     * read and parse control response
     * @return true on error, false on success.
     */
    bool get_response();

    /**
     * Is response a positive preliminary reply?
     * @return true or false.
     */
    bool is_positive_preliminary_reply() { return controlResponse[0] == '1'; }

    /**
     * Is response a positive completion reply?
     * @return true or false.
     */
    bool is_positive_completion_reply() { return controlResponse[0] == '2'; }

    /**
     * Is response a positive intermediate reply?
     * @return true or false.
     */
    bool is_positive_preliminary_reply() { return controlResponse[0] == '3'; }

    /**
     * Is response a negative transient reply?
     * @return true or false.
     */
    bool is_negative_transient_reply() { return controlResponse[0] == '4'; }

    /**
     * Is response a positive intermediate reply?
     * @return true or false.
     */
    bool is_negative_permanent_reply() { return controlResponse[0] == '5'; }

    /**
     * Is response a protected reply?
     * @return true or false.
     */
    bool is_protected_reply() { return controlResponse[0] == '6'; }

    /**
     * Is response a syntax error?
     * @return true or false.
     */
    bool is_syntax() { return controlResponse[1] == '0'; }

    /**
     * Is response informational?
     * @return true or false.
     */
    bool is_informational() { return controlResponse[1] == '1'; }

    /**
     * Is response referring to a change in connection state?
     * @return true or false.
     */
    bool is_connection() { return controlResponse[1] == '2'; }

    /**
     * Is response referring to an authoeization/authentication issue?
     * @return true or false.
     */
    bool is_authentication() { return controlResponse[1] == '3'; }

    /**
     * IS response filesystem related?
     * @return true or false.
     */
    bool is_filesystem_related() { return controlResponse[1] == '5'; }

protected:
private:
    /**
     * The hostname
     */
    string hostname;

    /**
     * The port number. (21 by default)
     */
    unsigned short port = 21;

    /**
     * The fnTCP client used for control connection
     */
    fnTcpClient control;

    /**
     * The fnTCP client used for data connection
     */
    fnTcpClient data;

    /**
     * last response from control connection.
     */
    string controlResponse;

    /**
     * Username
     */
    string username;

    /**
     * Password
     */
    string password;

    /**
     * Perform USER command on open control connection
     */
    void USER();

    /**
     * Perform PASS command on open control connection
     */
    void PASS();

    /**
     * Perform TYPE I command on open control connection
     */
    void TYPE();

};

#endif /* FNFTP_H */