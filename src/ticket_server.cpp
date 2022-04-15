#include <set>
#include <chrono>
#include <random>
#include <fstream>
#include <unordered_set>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "flags.h"
#include "ensure.h"
#include "buffer.h"

namespace {
    template<typename Number, std::enable_if_t<std::is_arithmetic_v<Number>, bool> = true>
    constexpr bool is_between(Number value, Number min, Number max) noexcept {
        return value >= min && value <= max;
    }
}

class TicketServer {
public:
    enum ClientRequest : uint8_t {
        GET_EVENTS      = 1, /** Request to list available events */
        GET_RESERVATION = 3, /** Request to reserve tickets to an event */
        GET_TICKETS     = 5  /** Request to buy reserved tickets */
    };

    enum ServerResponse : uint8_t {
        EVENTS      = 2,  /** Response to list available requests */
        RESERVATION = 4,  /** Response to confirm ticket reservation */
        TICKETS     = 6,  /** Response to send bought tickets */
        BAD_REQUEST = 255 /** Response to an invalid request */
    };

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
        debug("Starting listening on port", port);
    }

    ~TicketServer() {
        ensure(close(socket_fd) != -1, "Failed to close socket", socket_fd);
    }

    [[noreturn]] void start() {
        while (true) {
            sockaddr_in client{};
            size_t read_length = this->get_request(&client);

            if (read_length == 0) {
                debug("Received an empty request");
                continue;
            }

            try {
                this->remove_expired_reservations();
                this->handle_request(&client, read_length);
            } catch (const std::invalid_argument& e) {
                debug(e.what(), std::string(buffer, read_length));
            }
        }
    }

    static sockaddr_in get_address(uint16_t port) {
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

    static std::string response_name(ServerResponse response) {
        switch(response) {
            case EVENTS:
                return "EVENTS";
            case RESERVATION:
                return "RESERVATION";
            case TICKETS:
                return "TICKETS";
            default:
                return "BAD_REQUEST";
        }
    }

private:
    using desclen_t      = uint8_t;
    using tickets_t      = uint16_t;
    using event_id       = uint32_t;
    using reservation_id = uint32_t;
    using seconds_t      = uint64_t;
    using cookie_t       = std::string;

    using addr_ptr         = sockaddr_in*;
    using event_data       = std::pair<event_id, tickets_t>;
    using crypto_data      = std::pair<cookie_t, seconds_t>;
    using reservation_data = std::pair<crypto_data, event_data>;

    /** Message length of GET_EVENTS request */
    static constexpr size_t GET_EVENTS_LEN = 1;

    /** Message length of GET_RESERVATION request */
    static constexpr size_t GET_RESERVATION_LEN = 7;

    /** Maximum buffer size to store a single event information */
    static constexpr size_t MAX_EVENT_DATA = 262;

    /** Message length of GET_TICKETS request */
    static constexpr size_t GET_TICKETS_LEN     = 53;

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

    /** Character ranges for ticket codes */
    static constexpr char MIN_TICKET_DIGIT = '0';
    static constexpr char MAX_TICKET_DIGIT = '9';
    static constexpr char MIN_TICKET_ALPHA = 'A';
    static constexpr char MAX_TICKET_ALPHA = 'Z';

    int socket_fd = -1; /** Socket for IPv4 UDP connection */
    char buffer[MAX_DATAGRAM]; /** Communication buffer */
    seconds_t timeout; /** Time measured in seconds for a reservation to be valid */

    /** Mapping of event id's to their names and number of available tickets */
    std::unordered_map<event_id, std::pair<std::string, tickets_t>> events;

    /** Mapping of reservation id's to <(cookie, expiration time), (event id, tickets)> */
    std::map<reservation_id, reservation_data> reserved;

    /** Reservations expiring at the given time */
    std::map<seconds_t, std::unordered_set<reservation_id>> expiration;
    std::unordered_set<cookie_t> cookies; /** Cookies confirming reservations */

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
                events[event].first = name;
                std::stringstream(tickets_count) >> events[event].second;
            }
        }
    }

    void bind_socket(uint16_t port) {
        ensure(is_between(port, MIN_PORT, MAX_PORT), "Port must be from", MIN_PORT, "to", MAX_PORT);
        this->socket_fd = socket(AF_INET, SOCK_DGRAM, 0); /* IPv4 UDP socket */
        ensure(this->setup_address(get_address(port)) != -1, "Port", port, "requires root privileges");
    }

    int setup_address(const sockaddr_in& address) const {
        ensure(this->socket_fd > 0, "Failed to create a socket");
        auto address_len = static_cast<socklen_t>(sizeof(address));
        return bind(this->socket_fd, (sockaddr*) &address, address_len);
    }


    size_t get_request(addr_ptr client) {
        auto address_len = static_cast<socklen_t>(sizeof(*client));
        ssize_t message_len = recvfrom(socket_fd, buffer, MAX_DATAGRAM, 0,
                                       (sockaddr *) client, &address_len);

        std::string client_address = TicketServer::get_client_address(client);
        ensure(message_len >= 0, "Failed to receive a message from", client_address);
        debug("Received a message from", client_address);

        return static_cast<size_t>(message_len);
    }

    void send_response(addr_ptr client, size_t length) {
        auto address_len = static_cast<socklen_t>(sizeof(*client));
        ssize_t sent_len = sendto(socket_fd, buffer, length, 0,
                                  (sockaddr*) client, address_len);

        std::string client_address = TicketServer::get_client_address(client);
        bool all_bytes_sent = sent_len == static_cast<ssize_t>(length);

        ensure(all_bytes_sent, "Failed to send a message to", client_address);
        debug("Sent", response_name(static_cast<ServerResponse>(buffer[0])), "to", client_address);
    }

    static std::string get_client_address(addr_ptr client) {
        char* client_ip = inet_ntoa(client->sin_addr);
        uint16_t client_port = ntohs(client->sin_port);
        return std::string(client_ip) + ":" + std::to_string(client_port);
    }

    void remove_expired_reservations() {
        const seconds_t time = TicketServer::current_time();
        std::vector<decltype(expiration)::iterator> expired;

        /* Iterate over expired reservations */
        for (auto it = expiration.begin(); it->first < time && it != expiration.end(); it++) {
            expired.emplace_back(it);
            for (auto reservation : it->second) {
                this->remove_reservation(reservation);
            }
        }

        for (auto& expired_it : expired) {
            expiration.erase(expired_it);
        }
    }

    void remove_reservation(reservation_id reservation) {
        auto reservation_it = reserved.find(reservation);
        auto [crypto_info, event_info] = reservation_it->second;
        auto [cookie, expiration_time] = crypto_info;
        auto [event, tickets] = event_info;

        cookies.erase(cookie);
        reserved.erase(reservation_it);
        events[event].second += tickets;

        debug("Reservation", reservation, "has expired");
    }

    void handle_request(addr_ptr client, size_t request_len) {
        try {
            switch (buffer[0]) {
                case GET_EVENTS:
                    handle_get_events_request(client, request_len);
                    break;
                case GET_RESERVATION:
                    handle_get_reservation_request(client, request_len);
                    break;
                case GET_TICKETS:
                    handle_get_tickets_request(client, request_len);
                    break;
                default:
                    throw std::invalid_argument("Unknown request type");
            }
        } catch (const std::invalid_argument& e) {
            throw e;
        }
    }

    void handle_get_events_request(addr_ptr client, size_t request_len) {
        if (request_len != GET_EVENTS_LEN) {
            throw std::invalid_argument("GET_EVENTS request is too long");
        }

        this->send_events(client);
    }

    void send_events(addr_ptr client) {
        size_t packed_bytes = buffer_write(buffer, EVENTS);

        /* Naively pack as many events as we can */
        for (auto& [event_id, event_info] : events) {
            char portion[MAX_EVENT_DATA];
            auto [description, tickets] = event_info;
            size_t portion_bytes = buffer_write(portion, htonl(event_id), htons(tickets),
                                                static_cast<desclen_t>(description.size()),
                                                static_cast<std::string>(description));

            if (portion_bytes + packed_bytes > MAX_DATAGRAM)
                break;

            packed_bytes += buffer_write(buffer + packed_bytes, std::string(portion, portion_bytes));
        }

        this->send_response(client, packed_bytes);
    }

    void handle_get_reservation_request(addr_ptr client, size_t request_len) {
        if (request_len != GET_RESERVATION_LEN) {
            throw std::invalid_argument("GET_EVENTS request is too long");
        }

        auto event = htonl(*buffer_read<event_id>(buffer, 1));
        auto tickets = htons(*buffer_read<tickets_t>(buffer, 1 + sizeof(event)));
        auto event_it = events.find(event);

        /* Check if event exists and server can provide the given number of tickets */
        if (event_it != events.end() && valid_ticket_count(tickets, event_it->second.second)) {
            this->reserve_tickets(client, event, tickets);
        } else {
            this->send_bad_request<event_id>(client, event);
        }
    }

    static bool valid_ticket_count(tickets_t requested, tickets_t available) {
        return is_between(requested, static_cast<uint16_t>(1), std::min(MAX_TICKETS, available));
    }

    void reserve_tickets(addr_ptr client, event_id event, tickets_t tickets) {
        auto [reservation, cookie, expiration_time] = this->create_reservation(event, tickets);
        size_t bytes = buffer_write(buffer, RESERVATION, htonl(reservation), htonl(event),
                                    htons(tickets), cookie);

        bytes += buffer_write(buffer + bytes, htobe64(expiration_time));
        this->send_response(client, bytes);
    }

    std::tuple<reservation_id, cookie_t, seconds_t> create_reservation(event_id event, tickets_t tickets) {
        seconds_t expiration_time = timeout + TicketServer::current_time();
        reservation_id reservation = this->generate_reservation_id();
        cookie_t cookie = this->generate_cookie();

        events[event].second -= tickets;
        reserved[reservation] = {{cookie, expiration_time}, {event, tickets}};
        expiration[expiration_time].insert(reservation);
        cookies.insert(cookie);

        debug("Created reservation", reservation, "for", tickets, "tickets to event", event);

        return {reservation, cookie, expiration_time};
    }

    reservation_id generate_reservation_id() const {
        if (reserved.empty())
            return MIN_RESERVATION_ID;

        auto largest_reserved = reserved.rbegin()->first;
        if (largest_reserved <= std::numeric_limits<reservation_id>::max() - 1)
            return largest_reserved + 1;

        auto smallest_reserved = reserved.begin()->first;
        if (smallest_reserved >= std::numeric_limits<reservation_id>::min() + 1)
            return smallest_reserved - 1;

        /* Find the smallest id which is not a key in the map */
        auto last_in = [](auto& lhs, auto& rhs) {return lhs.first + 1 != rhs.first;};
        return 1 + std::adjacent_find(reserved.begin(), reserved.end(), last_in)->first;
    }

    cookie_t generate_cookie() const {
        cookie_t cookie(COOKIE_LEN, 0);
        std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<char> pick(MIN_COOKIE_CHAR, MAX_COOKIE_CHAR);

        do { /* Randomize and preserve uniqueness */
            std::generate_n(cookie.begin(), COOKIE_LEN, [&] {return pick(rng);});
        } while (cookies.find(cookie) != cookies.end());

        return cookie;
    }

    void handle_get_tickets_request(addr_ptr client, size_t request_len) {
        if (request_len != GET_TICKETS_LEN) {
            throw std::invalid_argument("GET_TICKETS request is too long");
        }

        auto reservation = htonl(*buffer_read<reservation_id>(buffer, 1));
        auto cookie = buffer_to_string(buffer, 1 + sizeof(reservation), COOKIE_LEN);
        auto reservation_it = reserved.find(reservation);

        /* Check if reservation exists and cookie match */
        if (reservation_it != reserved.end() && cookie == reservation_it->second.first.first) {
            auto [event, tickets_count] = reservation_it->second.second;
            this->send_tickets(client, reservation, tickets_count);
        } else {
            this->send_bad_request<reservation_id>(client, reservation);
        }
    }

    void send_tickets(addr_ptr client, reservation_id reservation, tickets_t tickets) {
        size_t bytes = buffer_write(buffer, TICKETS, htonl(reservation), htons(tickets));

        /* If client haven't sent a successful GET_TICKETS request */
        if (purchased.find(reservation) == purchased.end()) {
            this->assign_tickets(reservation, tickets);
            this->disable_expiration(reservation);
        }

        std::for_each_n(purchased[reservation].begin(), tickets, [&](auto& ticket) {
            bytes += buffer_write(buffer + bytes, ticket);
        });

        debug("Sending", tickets, "tickets for reservation", reservation);
        this->send_response(client, bytes);
    }

    void disable_expiration(reservation_id reservation) {
        seconds_t expiration_time = reserved[reservation].first.second;
        expiration[expiration_time].erase(reservation);
        debug("Disabled expiration for reservation", reservation);
    }

    void assign_tickets(reservation_id reservation, tickets_t ticket_count) {
        std::generate_n(std::back_inserter(purchased[reservation]), ticket_count, [this] {
            return this->generate_ticket();
        });
    }

    std::string generate_ticket() {
        std::string ticket = next_ticket;

        /* Increment last ticket code, digits are smaller than letters */
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

    template<typename T, std::enable_if_t<std::is_same_v<T, uint32_t>, bool> = true>
    void send_bad_request(addr_ptr client, T data) {
        if (data < MIN_RESERVATION_ID) { /* If T is event_id */
            debug("Illegal amount of tickets for event", data);
        } else { /* If T is reservation_id */
            debug("Invalid cookie or reservation", data, "does not exist");
        }

        size_t bytes = buffer_write(buffer, BAD_REQUEST, htonl(data));
        this->send_response(client, bytes);
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
