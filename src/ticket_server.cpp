#include <fstream>

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
        this->set_port(port);
        this->set_timeout(timeout);
        this->set_tickets(file);
    }

    void run() {

    }

private:
    uint16_t port;
    uint32_t timeout;
    std::unordered_map<std::string, size_t> tickets;

    void set_port(uint16_t port) {
        ensure(MIN_PORT <= port && port <= MAX_PORT, "Invalid port value");
        this->port = port;
    }

    void set_timeout(uint32_t timeout) {
        ensure(MIN_TIMEOUT <= timeout && timeout <= MAX_TIMEOUT, "Invalid timeout value");
        this->timeout = timeout;
    };

    void set_tickets(const std::string& file) {
        std::ifstream input(file);
        ensure(input.is_open(), "File", file, "does not exist");

        for (std::string film, tickets_count; getline(input, film); ) {
            if (getline(input, tickets_count)) {
                std::stringstream(tickets_count) >> tickets[film];
            }
        }
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
