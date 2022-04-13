from test_commandline_params import test_commandline_params
from test_small_correctness import test_small_correctness
from test_big_correctness import test_big_correctness
from test_limits import test_limits
from test_empty_file import test_empty_file
from test_reservation_timing_out import test_reservation_timing_out
from test_invalid_requests import test_invalid_requests

import os

if __name__ == '__main__':
    tests = [
        test_commandline_params,
        test_small_correctness,
        test_big_correctness,
        test_limits,
        test_reservation_timing_out,
        test_empty_file,
        test_invalid_requests
    ]
    
    try:
        for t in tests:
            print(t.__name__ + '...')
            t()
        print('Tests passed!')
    except Exception as ex:
        os.system("pkill ticket_server")
        raise ex
        
