#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

static int get_ip_address(const char *name, int family, char *host);
static int copy(int read_fd, int write_fd, uint8_t *buffer, int buffer_size);
static void convert_address(const char *address, struct sockaddr_storage *addr);
static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void start_listening(int server_fd, int backlog);
static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len);

enum
{
    BUF_SIZE = 1024,
};

int main(void)
{
    const char *name;
    int         family;
    char        host_ip[NI_MAXHOST];
    char        dest_ip[NI_MAXHOST];
    in_port_t   src_port = 4985;
    in_port_t   dest_port = 4981;
    int         result;
    bool        server;
    bool        client;
    int         accept_fd;
    int         read_fd;
    int         write_fd;
    uint8_t     buffer[BUF_SIZE];

    name   = "en0";
    family = AF_INET;
    memset(host_ip, '\0', NI_MAXHOST);
    memset(dest_ip, '\0', NI_MAXHOST);
    result = get_ip_address(name, family, host_ip);
    strcpy(dest_ip, "192.168.0.19");

    if(result == -1)
    {
        return EXIT_FAILURE;
    }

    if(result == 0)
    {
        fprintf(stderr, "Cannot find %s for %i\n", name, family);
        return EXIT_FAILURE;
    }

    printf("found %s for %i IP = %s\n", name, family, host_ip);
    server   = false;
    client   = true;
    read_fd  = STDIN_FILENO;
    write_fd = STDOUT_FILENO;
    accept_fd = -1;

    if(server)
    {
        struct sockaddr_storage addr;
        struct sockaddr_storage client_addr;
        socklen_t               client_addr_len;

        accept_fd = socket(family, SOCK_STREAM, 0);
        if (accept_fd == -1)
        {
            perror("socket");
            return EXIT_FAILURE;
        }

        convert_address(host_ip, &addr);
        socket_bind(accept_fd, &addr, src_port);
        start_listening(accept_fd, SOMAXCONN);
        read_fd = socket_accept_connection(accept_fd, &client_addr, &client_addr_len);
    }

    if(client)
    {
        struct sockaddr_storage addr;

        write_fd = socket(family, SOCK_STREAM, 0);

        if (write_fd == -1)
        {
            perror("socket");
            return EXIT_FAILURE;
        }

        convert_address(dest_ip, &addr);
        socket_connect(write_fd, &addr, dest_port);
    }

    copy(read_fd, write_fd, buffer, BUF_SIZE);

    if(server)
    {
        close(read_fd);

        if (accept_fd != -1)
        {
            close(accept_fd);
        }
    }

    if(client)
    {
        close(write_fd);
    }

    return EXIT_SUCCESS;
}

static int get_ip_address(const char *name, int family, char *host)
{
    struct ifaddrs       *interfaces;
    const struct ifaddrs *ifaddr;

    // Get the list of network interfaces
    if(getifaddrs(&interfaces) == -1)
    {
        perror("getifaddrs");
        return -1;
    }

    for(ifaddr = interfaces; ifaddr != NULL; ifaddr = ifaddr->ifa_next)
    {
        if(ifaddr->ifa_addr == NULL)
        {
            continue;
        }

        if(ifaddr->ifa_addr->sa_family == family)
        {
            if(strcmp(name, ifaddr->ifa_name) == 0)
            {
                if(ifaddr->ifa_addr->sa_family == AF_INET)
                {
                    struct sockaddr_in ipv4;

                    memcpy(&ipv4, ifaddr->ifa_addr, sizeof(struct sockaddr_in));
                    inet_ntop(AF_INET, &(ipv4.sin_addr), host, NI_MAXHOST);

                }
                else if(ifaddr->ifa_addr->sa_family == AF_INET6)
                {
                    struct sockaddr_in6 ipv6;

                    memcpy(&ipv6, ifaddr->ifa_addr, sizeof(struct sockaddr_in6));
                    inet_ntop(AF_INET6, &(ipv6.sin6_addr), host, NI_MAXHOST);
                }
                else
                {
                    continue;
                }

                return 1;
            }
        }
    }

    freeifaddrs(interfaces);

    return 0;
}

static int copy(int read_fd, int write_fd, uint8_t *buffer, int buffer_size)
{
    ssize_t nread;

    do
    {
        ssize_t nwrote;

        nread = read(read_fd, buffer, buffer_size);

        if (nread == -1)
        {
            perror("read");

            return -1;
        }

        if (nread > 0)
        {
            nwrote = write(write_fd, buffer, nread);

            if (nwrote == -1)
            {
                perror("read");

                return -1;
            }
        }
    }
    while (nread > 0);

    return 0;
}

static void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    if (inet_pton(AF_INET, address,
                  &((struct sockaddr_in *)addr)->sin_addr) == 1)
    {
        struct sockaddr_in *a = (struct sockaddr_in *)addr;
        a->sin_family = AF_INET;
        a->sin_len    = sizeof(struct sockaddr_in);
    }
    else if (inet_pton(AF_INET6, address,
                       &((struct sockaddr_in6 *)addr)->sin6_addr) == 1)
    {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)addr;
        a->sin6_family = AF_INET6;
        a->sin6_len    = sizeof(struct sockaddr_in6);
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char addr_str[INET6_ADDRSTRLEN];
    struct sockaddr *sa = (struct sockaddr *)addr;

    if (sa->sa_family == AF_INET)
    {
        struct sockaddr_in *a = (struct sockaddr_in *)addr;
        a->sin_port = htons(port);

        if (inet_ntop(AF_INET, &a->sin_addr, addr_str, sizeof(addr_str)) == NULL)
        {
            perror("inet_ntop");
            exit(EXIT_FAILURE);
        }

        printf("Connecting to: %s:%u\n", addr_str, port);

        if (connect(sockfd, (struct sockaddr *)a, a->sin_len) == -1)
        {
            fprintf(stderr, "Error: connect (%d): %s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    else if (sa->sa_family == AF_INET6)
    {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)addr;
        a->sin6_port = htons(port);

        if (inet_ntop(AF_INET6, &a->sin6_addr, addr_str, sizeof(addr_str)) == NULL)
        {
            perror("inet_ntop");
            exit(EXIT_FAILURE);
        }

        printf("Connecting to: %s:%u\n", addr_str, port);

        if (connect(sockfd, (struct sockaddr *)a, a->sin6_len) == -1)
        {
            fprintf(stderr, "Error: connect (%d): %s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        fprintf(stderr, "Invalid address family: %d\n", sa->sa_family);
        exit(EXIT_FAILURE);
    }

    printf("Connected to: %s:%u\n", addr_str, port);
}

static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    socklen_t addr_len;
    void     *vaddr;
    in_port_t net_port;

    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        addr_len            = sizeof(*ipv4_addr);
        ipv4_addr->sin_port = net_port;
        vaddr               = (void *)&(((struct sockaddr_in *)addr)->sin_addr);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        addr_len             = sizeof(*ipv6_addr);
        ipv6_addr->sin6_port = net_port;
        vaddr                = (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr);
    }
    else
    {
        fprintf(stderr, "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(inet_ntop(addr->ss_family, vaddr, addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Binding to: %s:%u\n", addr_str, port);

    if(bind(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        perror("Binding failed");
        fprintf(stderr, "Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    printf("Bound to socket: %s:%u\n", addr_str, port);
}

static void start_listening(int server_fd, int backlog)
{
    if(listen(server_fd, backlog) == -1)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for incoming connections...\n");
}

static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len)
{
    int  client_fd;
    char client_host[NI_MAXHOST];
    char client_service[NI_MAXSERV];

    errno     = 0;
    client_fd = accept(server_fd, (struct sockaddr *)client_addr, client_addr_len);

    if(client_fd == -1)
    {
        if(errno != EINTR)
        {
            perror("accept failed");
        }

        return -1;
    }

    if(getnameinfo((struct sockaddr *)client_addr, *client_addr_len, client_host, NI_MAXHOST, client_service, NI_MAXSERV, 0) == 0)
    {
        printf("Accepted a new connection from %s:%s\n", client_host, client_service);
    }
    else
    {
        printf("Unable to get client information\n");
    }

    return client_fd;
}