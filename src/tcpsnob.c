#include <stdlib.h>     // NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <unistd.h>     // close(), fcntl()
#include <errno.h>      // errno
#include <fcntl.h>      // fcntl()
#include <sys/types.h>  // ssize_t
#include <sys/socket.h> // socket(), connect(), send(), recv()
#include <netdb.h>      // getaddrinfo()
#include "tcpsnob.h"

/*
 * Creates a non-blocking TCP socket, either IPv4 or IPv6, depending on ip_type.
 * If the given ip_type is neither AF_INET nor AF_INET6, AF_INET (IPv4) is used.
 * Returns the socket's file descriptor on success or -1 on error. Check errno.
 * In case of error, either we failed to create a socket via socket(), or a call
 * to fcntl(), in an attempt to get or set the file descriptor flags, failed.
 */
int tcpsnob_create(int ip_type)
{
	// If ip_type was neither IPv4 nor IPv6, we fall back to IPv4
	if ((ip_type != AF_INET) && (ip_type != AF_INET6))
	{
		ip_type = AF_INET;
	}
	
	// This line could replace all of the follwing but isn't POSIX!
	// int sfd = socket(s->ip_type, SOCK_STREAM | SOCK_NONBLOCK, 0);
	
	// Open a TCP socket (SOCK_STREAM)
	int sfd = socket(ip_type, SOCK_STREAM, 0);
	if (sfd == -1)
	{
		return -1;
	}

	// Get the current file descriptor flags
	int get = fcntl(sfd, F_GETFL);
	if (get == -1)
	{
		return -1;
	}

	// Add O_NONBLOCK to the file descriptor flags
	int set = fcntl(sfd, F_SETFL, get | O_NONBLOCK);
	if (set == -1)
	{
		return -1;
	}

	// All done, return socket file descriptor
	return sfd;
}

/*
 * Initiates a connection for the TCP socket described by sockfd.
 * The ip_type should match the one used when the socket was created.
 * If the given ip_type is neither AF_INET nor AF_INET6, AF_INET (IPv4) is used.
 * Returns 0 if the connection was successfully initiated (is now in progress).
 * Returns -1 if the host/port could not be translated into an IP address or if 
 * the connection could not be established for some other reason.
 */
int tcpsnob_connect(int sockfd, int ip_type, const char *host, const char *port)
{
	// If ip_type was neither IPv4 nor IPv6, we fall back to IPv4
	if ((ip_type != AF_INET) && (ip_type != AF_INET6))
	{
		ip_type = AF_INET;
	}

	// Not initializing the struct with { 0 } will result in garbage values
	// that can (but not necessarily will) make getaddrinfo() fail!
	struct addrinfo hints = { 0 };
	hints.ai_family   = ip_type;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	struct addrinfo *info = NULL;
	if (getaddrinfo(host, port, &hints, &info) != 0)
	{
		// Calling freeaddrinfo() when getaddrinfo() failed
		// will give a segfault in case `info` is still NULL!
		if (info != NULL)
		{
			freeaddrinfo(info);
		}
		return -1;
	}

	// Attempt to initiate a connection
	int con = connect(sockfd, info->ai_addr, info->ai_addrlen);
	freeaddrinfo(info);

	// connect() should return -1 for non-blocking sockets
	if (con == -1)
	{
		// Connection in progress (that's what we expect!)
		if (errno == EINPROGRESS || errno == EALREADY)
		{
			return 0;
		}
		// Some other error occured (errno will be set)
		return -1;
	}

	// connect() returned 0, so we're connected (erm... how?)
	return 0;
}

/*
 * Queries getsockopt() for the socket status in an attempt to figure out
 * whether the socket is connected. Note that this should not be used unless
 * there is a good reason - it is always best to simply try and send on the
 * socket in question to see if it is connected. If you want to check if a 
 * previous connection attempt succeeded, you should simply use select(), 
 * poll() or epoll() to wait on the socket and see if it becomes ready for 
 * writing (sending); this indicates the socket connection is established.
 * Returns 0 if the socket is healthy and most likely connected.
 * Returns -1 if the socket reported an error or the socket status could not
 * be queried, both indicating that the socket is most likely not connected.
 */
int tcpsnob_status(int sockfd)
{
	int err = 0;
	socklen_t len = sizeof(err);

	if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &len) != 0)
	{
		// Could not get the socket's status, invalid file descriptor?
		return -1;
	}

	if (err != 0)
	{
		// Socket reported some error, so probably not connected
		return -1;
	}

	// No socket error reported, chances are it is connected
	return 0;
}

/*
 * Sends the given data using the given socket.
 * On success, this function returns the number of bytes sent.
 * On error, -1 is returned and errno is set appropriately.
 * See the man page of send() for more details.
 */
int tcpsnob_send(int sockfd, const char *msg, size_t len)
{
	return send(sockfd, msg, len, 0);
}

/*
 * Reads the buffer of the given socket using recv().
 * On success, this function returns the number of bytes received.
 * On error, -1 is returend and errno is set appropriately.
 * Returns 0 if the if the socket connection was shut down by peer
 * or if the requested number of bytes to receive from the socket was 0.
 * See the man page of recv() for more details.
 */
int tcpsnob_receive(int sockfd, char *buf, size_t len)
{
	return recv(sockfd, buf, len, 0);
}

/*
 * Closes the given socket.
 * Returns 0 on success, -1 on error (see errno).
 * See the man page of close() for more details.
 */
int tcpsnob_close(int sockfd)
{
	return close(sockfd);
}

