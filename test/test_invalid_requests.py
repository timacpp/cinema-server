from basic_client import Client, Response255Exception
from server_wrap import start_server
import struct

MIN_RESERVATION = 999999 + 1
COOKIE = ('!' * 48).encode()

# Does nothing if server ignored expr. Expr is not GET_TICKETS
def try_ignore(expr, client, info):
    client.get_tickets(info.reservation_id, info.cookie)

# Does nothing if server ignored expr. Expr is not GET_EVENTS
def try_ignore_tickets(expr, client):
    client.get_events()

def test_invalid_requests():
    server = start_server('event_files/single_event', timeout=777)
    client = Client()
    info = client.get_reservation(0, 1)

    # Invalid message_id
    try_ignore(client.send_message(struct.pack('!B', 0)), client, info)
    try_ignore(client.send_message(struct.pack('!B', 2)), client, info)
    try_ignore(client.send_message(struct.pack('!B', 3)), client, info)
    try_ignore(client.send_message(struct.pack('!B', 5)), client, info)
    try_ignore(client.send_message(struct.pack('!B', 255)), client, info)

    # Extra byte at the end
    try_ignore(client.send_message(struct.pack('!BB', 1, 3)), client, info)
    try_ignore(client.send_message(struct.pack('!BI', 3, 0)), client, info)
    try_ignore(client.send_message(struct.pack('!BIHB', 3, 0, 1, 0)), client, info)
    try_ignore_tickets(client.send_message(struct.pack('!BI48sB', 5, MIN_RESERVATION, COOKIE, 0)), client)

    # Lacking byte
    try_ignore(client.send_message(struct.pack('!BI', 3, 0)), client, info)
    try_ignore_tickets(client.send_message(struct.pack('!BI', 5, MIN_RESERVATION)), client)

    server.terminate()
    server.communicate()

