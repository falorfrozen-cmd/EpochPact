"""Load an explicitly named offline character through the game's normal menu.

Verification helper only. No save writes or automatic progression actions.
"""
import argparse
import json
import time
from . import le_session as le


def wait_state(state, timeout=45):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if le.modern_ipc(le.GAME / 'EpochPact/ipc'):
            reply = le.send('sessionread', timeout=2)
            if reply:
                data = json.loads(reply)
                if data.get('state') == state and data.get('transitioning') is False:
                    return data
        time.sleep(.2)
    raise RuntimeError('Normal menu transition did not finish: ' + state)


def load(name, level):
    wait_state('Login')
    print(le.send('playoffline', timeout=5), flush=True)
    wait_state('CharacterSelect')
    print(le.send(f'loadname {name} {level}', timeout=15), flush=True)
    wait_state('InGame')
    identity = json.loads(le.send('identityread', timeout=5))
    assert identity['player']['name'] == name, identity
    print(json.dumps(identity), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('name'); parser.add_argument('level', type=int)
    args = parser.parse_args()
    load(args.name, args.level)
