#include <set>
#include <chrono>
#include <random>
#include <fstream>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "flags.h"
#include "ensure.h"
#include "buffer.h"

namespace {
    template<typename Number, std::enable_if_t<std::is_arithmetic_v<Number>, bool> = true>
    constexpr bool is_between(Number value, Number min, Number max) noexcept {
        return value >= min && value <= max;
    }
}

struct ClientRequest {
    /** Message length of GET_EVENTS request */
    static constexpr size_t GET_EVENTS_LEN      = 1;

    /** Message length of GET_RESERVATION request */
    static constexpr size_t GET_RESERVATION_LEN = 7;

    /** Message length of GET_TICKETS request */
    static constexpr size_t GET_TICKETS_LEN     = 53;

    enum Type : uint8_t {
        GET_EVENTS      = 1, /** Request to list available events */
        GET_RESERVATION = 3, /** Request to reserve tickets to an event */
        GET_TICKETS     = 5  /** Request to buy reserved tickets */
    };
};

struct ServerResponse {
    /** Maximum buffer size to store a single event information */
    static constexpr size_t MAX_EVENT_DATA = 262;

    enum Type : uint8_t {
        EVENTS      = 2,  /** Response to list available requests */
        RESERVATION = 4,  /** Response to confirm ticket reservation */
        TICKETS     = 6,  /** Response to send bought tickets */
        BAD_REQUEST = 255 /** Response to an invalid request */
    };
};

class TicketServer {
public:
    static constexpr uint32_t MIN_TIMEOUT     = 1;
    static constexpr uint32_t DEFAULT_TIMEOUT = 5;
    static constexpr uint32_t MAX_TIMEOUT     = 86400;

    static constexpr uint16_t MIN_PORT     = 0;
    static constexpr uint16_t DEFAULT_PORT = 2022;
    static constexpr uint16_t MAX_PORT     = 65535;

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

    static sockaddr_in address(uint16_t port) {
        sockaddr_in address{};

        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_ANY);

        return address;
    }

    static uint64_t current_time() {
        using namespace std::chrono;
        return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    }

private:
    using desclen_t        = uint8_t;
    using tickets_t        = uint16_t;
    using event_id         = uint32_t;
    using reservation_id   = uint32_t;
    using addr_ptr         = sockaddr_in*;
    using reservation_data = std::pair<event_id, tickets_t>;

    /** Server buffer size */
    static constexpr size_t MAX_DATAGRAM = 65507;

    /** Minimal possible ID of a reservation */
    static constexpr reservation_id MIN_RESERVATION_ID = 1000000;

    /** Length of a cookie to confirm a reservation */
    static constexpr size_t COOKIE_LEN = 48;

    /** Character range for cookie */
    static constexpr char MIN_COOKIE_CHAR = 33;
    static constexpr char MAX_COOKIE_CHAR = 126;

    /** Maximal number of tickets to reserve in a single request */
    static constexpr tickets_t MAX_TICKETS = 9357;

    /** Length of a ticket code */
    static constexpr size_t TICKET_LEN = 7;

    /** Character ranges for ticket codes */
    static constexpr char MIN_TICKET_DIGIT = '0';
    static constexpr char MAX_TICKET_DIGIT = '9';
    static constexpr char MIN_TICKET_ALPHA = 'A';
    static constexpr char MAX_TICKET_ALPHA = 'Z';

    int socket_fd = -1; /** Socket of communication */
    char buffer[MAX_DATAGRAM]; /** Communication buffer */

    std::unordered_map<std::string, event_id> events; /** Mapping of event names to their id's */
    std::unordered_map<event_id, tickets_t> tickets; /** Number of available tickets for events */

    uint32_t timeout; /** Time for a reservation to be valid */
    std::map<reservation_id, reservation_data> reserved; /** Reserved tickets for events */
    std::unordered_map<std::string, reservation_id> cookies; /** Cookies to confirm reservations */

    std::string next_ticket = "0000000"; /** Ticket code for the next purchase */
    std::unordered_map<reservation_id, std::vector<std::string>> purchased; /** Purchase history */

    void set_timeout(uint32_t _timeout) {
        ensure(is_between(_timeout, MIN_TIMEOUT, MAX_TIMEOUT), "Invalid timeout value");
        this->timeout = _timeout;
    };

    void initialize_database(const std::string& file_db) {
        std::ifstream file(file_db);
        std::string name, tickets_count;
        ensure(file.is_open(), "File", file_db, "does not exist");

        for (event_id event = 0; getline(file, name); event++) {
            if (getline(file, tickets_count)) {
                events[name] = event;
                std::stringstream(tickets_count) >> tickets[event];
            }
        }
    }

    void bind_socket(uint16_t port) {
        ensure(is_between(port, MIN_PORT, MAX_PORT), "Invalid port value");
        this->socket_fd = socket(AF_INET, SOCK_DGRAM, 0); /* IPv4 UDP socket */
        ensure_errno(this->bind_address(address(port)), "Failed to bind address");
    }

    int bind_address(const sockaddr_in& address) const {
        ensure(this->socket_fd > 0, "Failed to create a socket with port");
        auto address_len = static_cast<socklen_t>(sizeof(address));
        return bind(this->socket_fd, (sockaddr*) &address, address_len);
    }

    // TODO: maybe redundant
    void clear_buffer() {
        std::fill_n(buffer, MAX_DATAGRAM, '\0');
    }

    size_t read_message(addr_ptr client) {
        auto address_len = static_cast<socklen_t>(sizeof(*client));
        ssize_t message_len = recvfrom(
                socket_fd, buffer, MAX_DATAGRAM, 0,
                (sockaddr *) client, &address_len
        );

        ensure(message_len >= 0, "Failed to receive a message");
        return static_cast<size_t>(message_len);
    }

    void send_message(addr_ptr client, size_t length) {
        auto address_len = static_cast<socklen_t>(sizeof(*client));
        ssize_t sent_len = sendto(
                socket_fd, buffer, length, 0,
                (sockaddr*) client, address_len
        );

        ensure(sent_len == static_cast<ssize_t>(length), "Failed to send a message");
    }

    void handle_request(addr_ptr client, size_t request_len) {
        try {
            switch (buffer[0]) {
                case ClientRequest::GET_EVENTS:
                    this->try_send_events(client, request_len);
                    break;
                case ClientRequest::GET_RESERVATION:
                    this->try_reserve_tickets(client, request_len);
                    break;
                case ClientRequest::GET_TICKETS:
                    this->try_send_tickets(client, request_len);
                    break;
                default:
                    throw std::invalid_argument("Unknown request type");
            }
        } catch (const std::invalid_argument& e) {
            throw;
        }
    }

    void try_send_events(addr_ptr client, size_t request_len) {
        if (request_len != ClientRequest::GET_EVENTS_LEN) {
            throw std::invalid_argument("GET_EVENTS request is too long");
        }

        this->send_events(client);
    }

    void send_events(addr_ptr client) {
        size_t packed_bytes = buffer_write(buffer, ServerResponse::EVENTS);

        for (auto& [description, id] : events) {
            char event_data[ServerResponse::MAX_EVENT_DATA];
            size_t event_bytes = buffer_write(event_data, htonl(id), htons(tickets[id]),
                                              static_cast<desclen_t>(description.size()),
                                              static_cast<std::string>(description));

            if (event_bytes + packed_bytes > MAX_DATAGRAM)
                break;

            packed_bytes += buffer_write(buffer + packed_bytes, std::string(event_data, event_bytes));
        }

        this->send_message(client, packed_bytes);
    }

    void try_reserve_tickets(addr_ptr client, size_t request_len) {
        if (request_len != ClientRequest::GET_RESERVATION_LEN) {
            throw std::invalid_argument("GET_EVENTS request is too long");
        }

        auto event = htonl(*buffer_read<event_id>(buffer, 1));
        auto tickets_count = htons(*buffer_read<tickets_t>(buffer, 1 + sizeof(event)));
        auto ticket_it = tickets.find(event);

        if (ticket_it == tickets.end() || !valid_tickets_count(tickets_count, ticket_it->second)) {
            this->send_bad_request(client, event);
        } else {
            this->reserve_tickets(client, event, tickets_count);
        }
    }

    static bool valid_tickets_count(tickets_t requested, tickets_t available) {
        return is_between(requested, static_cast<uint16_t>(1), std::min(MAX_TICKETS, available));
    }

    void reserve_tickets(addr_ptr client, event_id event, tickets_t tickets_count) {
        std::string cookie = this->generate_cookie();
        reservation_id reservation = this->generate_reservation_id();

        tickets[event] -= tickets_count;
        reserved[reservation] = {event, tickets_count};
        cookies[cookie] = reservation;

        size_t bytes = buffer_write(buffer, ServerResponse::RESERVATION,
                                    htonl(reservation), htonl(event),
                                    htons(tickets_count), cookie);

        bytes += buffer_write(buffer + bytes, htobe64(timeout + current_time()));
        this->send_message(client, bytes);
    }

    reservation_id generate_reservation_id() const {
        if (reserved.empty())
            return MIN_RESERVATION_ID;

        auto largest_reserved = reserved.rbegin()->first;
        if (largest_reserved <= std::numeric_limits<reservation_id>::max() - 1)
            return largest_reserved + 1;

        auto last_in = [](const auto& lhs, const auto& rhs) -> bool {
            return lhs.first + 1 != rhs.first;
        };

        return 1 + std::adjacent_find(reserved.begin(), reserved.end(), last_in)->first;
    }

    std::string generate_cookie() const {
        std::string cookie(COOKIE_LEN, 0);
        std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<char> pick(MIN_COOKIE_CHAR, MAX_COOKIE_CHAR);

        do {
            std::generate_n(cookie.begin(), COOKIE_LEN, [&] {return pick(rng);});
        } while (cookies.find(cookie) != cookies.end());

        return cookie;
    }

    void try_send_tickets(addr_ptr client, size_t request_len) {
        if (request_len != ClientRequest::GET_TICKETS_LEN) {
            throw std::invalid_argument("GET_TICKETS request is too long");
        }

        auto reservation = htonl(*buffer_read<reservation_id>(buffer, 1));
        auto cookie = buffer_to_string(buffer, 1 + sizeof(reservation), COOKIE_LEN);
        auto reservation_it = reserved.find(reservation);

        if (reservation_it == reserved.end() || cookies.find(cookie) == cookies.end()) {
            this->send_bad_request(client, reservation);
        } else {
            this->send_tickets(client, reservation, reservation_it->second.second, cookie);
        }
    }

    void send_tickets(addr_ptr client, reservation_id reservation,
                      tickets_t ticket_count, const std::string& cookie) {
        size_t bytes = buffer_write(buffer, ServerResponse::TICKETS,
                                    htonl(reservation), htons(ticket_count));

        if (purchased.find(reservation) == purchased.end()) {
            this->register_tickets(reservation, ticket_count);
        }

        std::for_each_n(purchased[reservation].begin(), ticket_count,
                        [&](const auto& ticket) {
            bytes += buffer_write(buffer + bytes, ticket);
        });

        this->send_message(client, bytes);
    }

    void register_tickets(reservation_id reservation, tickets_t ticket_count) {
        std::generate_n(std::back_inserter(purchased[reservation]), ticket_count,
                        [this] {return this->generate_ticket();});
    }

    std::string generate_ticket() {
        std::string ticket = next_ticket;
        for (auto& letter : ticket) {
            if (letter == MAX_TICKET_ALPHA) {
                letter = MIN_TICKET_DIGIT;
            } else {
                if (letter == MAX_TICKET_DIGIT) {
                    letter = MIN_TICKET_ALPHA;
                } else {
                    letter++;
                }
                break;
            }
        }

        std::swap(ticket, next_ticket);

        return ticket;
    }

    template<typename T>
    void send_bad_request(addr_ptr client_address, T arg) {
        if constexpr (std::is_same_v<T, tickets_t>) {
            warn("Illegal amount of tickets", arg);
        } else {
            warn("Wrong cookie for reservation", arg);
        }

        size_t bytes = buffer_write(buffer, ServerResponse::BAD_REQUEST, arg);
        this->send_message(client_address, bytes);
    }
};

int main(int argc, char** argv) {
    flag_map flags = create_flag_map(argc, argv, "ftp");

    TicketServer server(
        get_flag_required<std::string>(flags, "-f"),
        get_flag<uint16_t>(flags, "-p").value_or(TicketServer::DEFAULT_PORT),
        get_flag<uint32_t>(flags, "-t").value_or(TicketServer::DEFAULT_TIMEOUT)
    );

    server.start();

    return EXIT_SUCCESS;
}
