# About the project
This is a UDP server for reserving cinema tickets. It is built upon raw socket communication, therefore messages are represented as small endian byte streams. 

# Running the server

To run the server, you one flag is required and two other are optional:

`-p <port>` – server listening port (2022 by default)

`-t <timeout>` – time limit for buying reserved tickets (5 seconds by default)

`-f <filename>` – path to file with initial events and tickets available. Example content:

```
fajny koncert
123
film o kotach
32
ZOO
0
Footer
```

# Client and requests

The directory `bin` contains the client code compiled on two different machines. Client can perform the following requests:

`GET_EVENTS` - request information on available tickets

`GET_RESERVATION <event_id> <ticket_count>` - request a reservation of a given number of tickets for an event

`GET_TICKETS <reservation_id> <cookie>` - request tickets for a given reservation, where cookie substitutes the need for an account

# Server responses

`EVENTS` - response with a list of pairs (event identifier, available tickets).

`RESERVATION` - response with reservation identifier, event identifier, ticket count, secure cookie and expiration time.

`TICKETS` - response with a unique ticket identifiers, one for each requested

`BAD_REQUEST` - response indicating an invalid request

# Libraries

`buffer.h` - fast, generic and variadic byte stream builder

`ensure.h` - logging and assertion library

`flags.h` - flag parser and validator
