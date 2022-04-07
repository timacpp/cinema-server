#include <fstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "flags.h"
#include "ensure.h"
#include "buffer.h"

namespace {
    template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, bool> = true>
    constexpr bool is_between(T value, T min, T max) noexcept {
        return value >= min && value <= max;
    }
}

struct ClientRequest {
public:
    static constexpr size_t GET_EVENTS_LEN      = 1;
    static constexpr size_t GET_RESERVATION_LEN = 7;
    static constexpr size_t GET_TICKETS_LEN     = 53;

    enum Type : uint8_t {
        GET_EVENTS      = 1,
        GET_RESERVATION = 3,
        GET_TICKETS     = 5
    };
};

struct ServerResponse {
public:
    static constexpr size_t MAX_EVENT_DATA = 262;

    enum Type : uint8_t {
        EVENTS      = 2,
        RESERVATION = 4,
        TICKETS     = 6,
        BAD_REQUEST = 255
    };

};

class TicketServer {
public:
    using desclen_t = uint8_t;

    static constexpr size_t MAX_DATAGRAM = 65507;

    static constexpr uint32_t MIN_TIMEOUT  = 1;
    static constexpr uint32_t MAX_TIMEOUT  = 86400;
    static constexpr uint32_t DFLT_TIMEOUT = 5;

    static constexpr uint16_t MIN_PORT  = 0;
    static constexpr uint16_t MAX_PORT  = 65535;
    static constexpr uint16_t DFLT_PORT = 2022;

    TicketServer(const std::string& file, uint16_t port, uint32_t timeout) {
        this->bind_socket(port);
        this->set_timeout(timeout);
        this->initialize_database(file);
    }

    ~TicketServer() {
        ensure_errno(close(socket_fd), "Failed to close socket", socket_fd);
    }

    [[noreturn]] void start() {
        while (true) {
            this->clear_buffer();

            sockaddr_in client_address;
            size_t read_length = this->read_message(&client_address);

            try {
                if (read_length > 0) {
                    this->handle_request(&client_address, read_length);
                } else {
                    throw std::invalid_argument("Request is empty");
                }
            } catch (const std::invalid_argument& e) {
                warn(e.what(), std::string(buffer, read_length));
            }
        }
    }

private:
    uint32_t timeout;
    int socket_fd = -1;
    char buffer[MAX_DATAGRAM];
    std::unordered_map<std::string, uint32_t> events;
    std::unordered_map<uint32_t, uint16_t> tickets;
    std::unordered_map<std::string, std::pair<uint32_t, uint16_t>> reserved;

    void set_timeout(uint32_t _timeout) {
        ensure(is_between(_timeout, MIN_TIMEOUT, MAX_TIMEOUT), "Invalid timeout value");
        this->timeout = _timeout;
    };

    void initialize_database(const std::string& file_db) {
        std::ifstream file(file_db);
        std::string film, tickets_count;
        ensure(file.is_open(), "File", file_db, "does not exist");

        for (uint32_t identity_key = 0; getline(file, film); identity_key++) {
            if (getline(file, tickets_count)) {
                events[film] = identity_key;
                std::stringstream(tickets_count) >> tickets[identity_key];
            }
        }
    }

    void bind_socket(uint16_t port) {
        ensure(is_between(port, MIN_PORT, MAX_PORT), "Invalid port value");
        this->socket_fd = socket(AF_INET, SOCK_DGRAM, 0); /* IPv4 UDP socket */
        ensure_errno(this->bind_address(create_address(port)), "Failed to bind address");
    }

    int bind_address(const sockaddr_in& address) const {
        ensure(this->socket_fd > 0, "Failed to create a socket with port");
        auto address_len = static_cast<socklen_t>(sizeof(address));
        return bind(this->socket_fd, (sockaddr*) &address, address_len);
    }

    void clear_buffer() {
        std::fill_n(buffer, MAX_DATAGRAM, '\0');
    }

    size_t read_message(sockaddr_in* client_address) {
        auto address_len = static_cast<socklen_t>(sizeof(*client_address));
        ssize_t message_len = recvfrom(
                socket_fd, buffer, MAX_DATAGRAM, 0,
                (sockaddr *) client_address, &address_len
        );

        ensure(message_len >= 0, "Failed to receive a message");
        return static_cast<size_t>(message_len);
    }

    void send_message(const sockaddr_in *client_address, size_t length = MAX_DATAGRAM) {
        auto address_len = static_cast<socklen_t>(sizeof(*client_address));
        ssize_t sent_len = sendto(
                socket_fd, buffer, length, 0,
                (sockaddr*) client_address, address_len
        );

        ensure(sent_len == static_cast<ssize_t>(length), "Failed to send a message");
    }

    void handle_request(sockaddr_in* client_address, size_t request_len) {
        try {
            switch (buffer[0]) {
                case ClientRequest::GET_EVENTS:
                    this->try_send_events(client_address, request_len);
                    break;
                case ClientRequest::GET_RESERVATION:
                    this->try_reserve_tickets(client_address, request_len);
                    break;
                case ClientRequest::GET_TICKETS:
                    this->try_send_tickets(client_address, request_len);
                    break;
                default:
                    throw std::invalid_argument("Unknown request type");
            }
        } catch (const std::invalid_argument& e) {
            throw;
        }
    }

    void try_send_events(sockaddr_in* client_address, size_t request_len) {
        if (request_len > ClientRequest::GET_EVENTS_LEN) {
            throw std::invalid_argument("GET_EVENTS request is too long");
        }

        this->send_events(client_address);
    }

    void send_events(sockaddr_in* client_address) {
        size_t packed_bytes = buffer_write(buffer, ServerResponse::EVENTS);

        for (auto& [event, id] : events) {
            char event_data[ServerResponse::MAX_EVENT_DATA];
            size_t event_bytes = buffer_write(
                    event_data, htonl(id), htons(tickets[id]),
                    static_cast<desclen_t>(event.size()),
                    const_cast<char*>(event.c_str())
            );

            if (event_bytes + packed_bytes > MAX_DATAGRAM)
                break;

            packed_bytes += buffer_write_n(buffer + packed_bytes, event_data, event_bytes);
        }

        this->send_message(client_address, packed_bytes);
    }

    void try_reserve_tickets(sockaddr_in* client_address, size_t request_len) {

    }

    void try_send_tickets(sockaddr_in* client_address, size_t request_len) {

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

    server.start();

    return EXIT_SUCCESS;
}
