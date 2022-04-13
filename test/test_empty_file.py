from basic_client import Client, Response255Exception
from server_wrap import start_server
from event_files.generate_file import generate_file

SMALLEST_ID = 0
RANDOM_ID = 42
BIGGEST_ID = 999999
COOKIE = '!' * 48
MIN_RESERVATION = BIGGEST_ID + 1

def test_empty_file():
    server = start_server('event_files/empty_events')
    client = Client()

    assert client.get_events() == []

    for event_id in [SMALLEST_ID, SMALLEST_ID + 1, RANDOM_ID, BIGGEST_ID]:
        try:
            client.get_reservation(event_id, 1)
            assert False
        except Response255Exception:
            pass

    try:
        client.get_tickets(MIN_RESERVATION, COOKIE)
        assert False
    except Response255Exception:
        pass

    server.terminate()
    server.communicate()
