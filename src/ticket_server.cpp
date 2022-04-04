#include <fstream>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "flags.h"
#include "ensure.h"

class TicketServer {
public:
    static constexpr uint32_t MIN_TIMEOUT = 1;
    static constexpr uint32_t MAX_TIMEOUT = 86400;
    static constexpr uint32_t DFLT_TIMEOUT = 5;

    // TODO: Should I use UDP port range?
    static constexpr uint16_t MIN_PORT = 1024;
    static constexpr uint16_t MAX_PORT = 65535;
    static constexpr uint16_t DFLT_PORT = 2022;

    TicketServer(const std::string& file, uint16_t port, uint32_t timeout) {
        this->bind_socket(port);
        this->set_timeout(timeout);
        this->initialize_database(file);
    }

    void run() {

    }

private:
    int socket_fd = -1;
    uint32_t timeout;
    std::unordered_map<std::string, size_t> tickets;

    void set_timeout(uint32_t _timeout) {
        ensure(MIN_TIMEOUT <= _timeout
               && _timeout <= MAX_TIMEOUT, "Invalid timeout value");
        this->timeout = _timeout;
    };

    void initialize_database(const std::string& file_db) {
        std::ifstream file(file_db);
        ensure(file.is_open(), "File", file_db, "does not exist");

        for (std::string film, tickets_count; getline(file, film); ) {
            if (getline(file, tickets_count)) {
                std::stringstream(tickets_count) >> tickets[film];
            }
        }
    }

    void bind_socket(uint16_t port) {
        ensure(MIN_PORT <= port && port <= MAX_PORT, "Invalid port value");
        this->socket_fd = socket(AF_INET, SOCK_DGRAM, 0); /* IPv4 UDP socket */
        ensure(this->socket_fd > 0, "Failed to create a socket with port", port);
        ensure_errno(this->bind_address(create_address(port)), "Failed to bind address");
    }

    int bind_address(const sockaddr_in& address) const {
        ensure(this->socket_fd > 0, "Cannot bind address to uninitialized socket");
        return bind(this->socket_fd, (sockaddr*) &address, (socklen_t) sizeof(address));
    }

    static sockaddr_in create_address(uint16_t port) {
        sockaddr_in address;

        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_ANY);

        return address;
    }
};

int main(int argc, char** argv) {
    flag_map flags = create_flag_map(argc, argv, "ftp");

    TicketServer server(
        get_flag_required<std::string>(flags, "-f"),
        get_flag<uint16_t>(flags, "-p").value_or(TicketServer::DFLT_PORT),
        get_flag<uint32_t>(flags, "-t").value_or(TicketServer::DFLT_TIMEOUT)
    );

    server.run();

    return EXIT_SUCCESS;
}
