#include <stdlib.h>	// NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <unistd.h>	// close(), fcntl()
#include <fcntl.h>	// fcntl()
#include <sys/types.h>	// ssize_t
#include <sys/socket.h> // socket(), connect(), send(), recv()
#include <netdb.h>	// getaddrinfo()

/*
 * Creates a non-blocking TCP socket, either IPv4 or IPv6, depending on ip_type.
 * If the given ip_type is neither AF_INET nor AF_INET6, AF_INET (IPv4) is used.
 * Returns the socket's file descriptor on success.
 * Returns -1 if socket() failed to create a socket, check errno.
 * Returns -2 if fcntl() failed to get the file descriptor flags, check errno.
 * Returns -3 if fcntl() failed to set the file descriptor flags, check errno.
 */
int stcpnb_create(int ip_type)
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
		return -2;
	}

	// Add O_NONBLOCK to the file descriptor flags
	int set = fcntl(sfd, F_SETFL, get | O_NONBLOCK);
	if (set == -1)
	{
		return -3;
	}

	// All done, return socket file descriptor
	return sfd;
}

/*
 * Initiates a connection for the TCP socket described by sockfd.
 * The ip_type should match the one used when the socket was created.
 * If the given ip_type is neither AF_INET nor AF_INET6, AF_INET (IPv4) is used.
 * Returns  0 if the connection was successfully initiated (is now in progress).
 * Returns -1 if connect() failed, in which case errno will be set.
 * Returns -2 if getaddrinfo() failed to translate host/port to an IP.
 */
int stcpnb_connect(int sockfd, int ip_type, const char *host, const char *port)
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
		return -2;
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
		// Some other error occured (damn)
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
 * Returns  0 if the socket is healthy and probably connected.
 * Returns -1 if the socket reported an error and is probably disconnected.
 * Returns -2 if the socket status could not be queried.
 */

int stcpnb_status(int sockfd)
{
	int err = 0;
	socklen_t len = sizeof(err);

	if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &len) != 0)
	{
		// Could not get the socket's status, invalid file descriptor?
		return -2;
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
 *
 */
int stcpnb_send(int sockfd, const char *msg, size_t len)
{
	/* 
	 * On success, these calls return the number of bytes sent.  On error,
	 * -1 is returned, and errno is set appropriately.
	 */
	return send(sockfd, msg, len, 0);
}

/*
 *
 */
int stcpnb_receive(int sockfd, char *buf, size_t len)
{
	/*
	 * These calls return the number of bytes received, or -1 if an error
	 * occurred.  In the event of an error, errno is set to indicate the
	 * error.
	 *
	 * When a stream socket peer has performed an orderly shutdown, the
         * return value will be 0 (the traditional "end-of-file" return).
	 *
         * The value 0 may also be returned if the requested number of bytes to
         * receive from a stream socket was 0.
	 */
	return recv(sockfd, buf, len, 0);
}

/*
 * Returns 0 on succcess, -1 on error (see errno).
 */
int stcpnb_close(int sockfd)
{
	return close(sockfd);
}

