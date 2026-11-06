"""Local room prototype. All C calls remain serial."""
import ctypes
import json
import os
import ipaddress
import secrets
import sqlite3
import string
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlsplit
import storage

ROOT = Path(__file__).resolve().parent
BIND_HOST = os.environ.get('MALLANGI_HOST', '127.0.0.1')
ipaddress.IPv4Address(BIND_HOST)
ALLOWED_ORIGINS = {'http://localhost:8000', 'http://127.0.0.1:8000'}
for value in os.environ.get('MALLANGI_ORIGINS', '').split(','):
    origin = value.strip()
    if not origin:
        continue
    parsed = urlsplit(origin)
    if (parsed.scheme != 'http' or not parsed.hostname or parsed.port != 8000
            or parsed.path or parsed.query or parsed.fragment
            or parsed.username or parsed.password or '*' in origin):
        raise ValueError('MALLANGI_ORIGINS must contain exact http://host:8000 origins')
    ALLOWED_ORIGINS.add(origin)
LIBRARY = Path(os.environ.get('MALLANGI_LIBRARY', str(ROOT.parents[1] / 'build/team/libmallangi_web.so'))).resolve()
os.environ.setdefault('MALMOI_NATIVE_INDEX', str(LIBRARY.parent / 'runtime/index'))
os.environ.setdefault('MALMOI_KIWI_MODEL', str(LIBRARY.parent / 'runtime/model/models/cong/base'))
engine = ctypes.CDLL(str(LIBRARY))
engine.mallangi_create.argtypes = []
engine.mallangi_create.restype = ctypes.c_void_p
engine.mallangi_destroy.argtypes = [ctypes.c_void_p]
engine.mallangi_destroy.restype = None
engine.mallangi_request.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
engine.mallangi_request.restype = ctypes.c_char_p
engine.mallangi_join.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
engine.mallangi_join.restype = ctypes.c_char_p
engine.mallangi_saved_answer.argtypes = [ctypes.c_void_p]
engine.mallangi_saved_answer.restype = ctypes.c_char_p
engine.mallangi_restore.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_float), ctypes.c_int]
engine.mallangi_restore.restype = ctypes.c_int
engine.mallangi_saved_sense_id.argtypes=[ctypes.c_void_p]
engine.mallangi_saved_sense_id.restype=ctypes.c_int64
engine.mallangi_saved_has_sense.argtypes=[ctypes.c_void_p]
engine.mallangi_saved_has_sense.restype=ctypes.c_int
engine.mallangi_restore_sense.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_int,ctypes.c_int64,ctypes.POINTER(ctypes.c_char_p),ctypes.POINTER(ctypes.c_float),ctypes.c_int]
engine.mallangi_restore_sense.restype=ctypes.c_int
STATE_FILE = Path(os.environ.get('MALLANGI_STATE', str(ROOT / 'mallangi-state.sqlite3')))
sessions = {}
rooms = {}
TTL = 3600
MAX_SESSIONS = 64
MAX_PLAYERS = 8
MAX_ATTEMPTS = 5


def rank_players(players):
    """Fewest valid submissions wins; equal counts share competition rank."""
    players.sort(key=lambda p: (
        0 if p['won'] else 1 if not p['finished'] else 2,
        p['attemptsUsed'] if p['won'] else 0,
        p['number'],
    ))
    previous_attempts = None
    previous_rank = None
    for position, player in enumerate(players, 1):
        player['rank'] = None
        if player['won']:
            if player['attemptsUsed'] != previous_attempts:
                previous_rank = position
                previous_attempts = player['attemptsUsed']
            player['rank'] = previous_rank
    return players


def leave_room(token):
    session = sessions[token]
    code = session.pop('room', None)
    if code in rooms:
        rooms[code]['members'].discard(token)
        if not rooms[code]['members']:
            engine.mallangi_destroy(rooms.pop(code)['handle'])


def expire_sessions():
    now = time.time()
    for token, session in list(sessions.items()):
        if now - session['touched'] > TTL:
            leave_room(token)
            engine.mallangi_destroy(sessions.pop(token)['handle'])


def session_view(token, status=None):
    session = sessions[token]
    state = dict(session['state'])
    state.update(sessionId=token, roomCode=session.get('room'),
                 name=session.get('name', ''), history=list(session['history']))
    if status is not None:
        state['status'] = status
    return state


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def reply(self, code, body, kind='application/json; charset=utf-8'):
        if self.command == 'POST' and 200 <= code < 300:
            try:
                storage.save(STATE_FILE, engine, sessions, rooms)
            except (OSError, sqlite3.Error, ValueError) as error:
                self.log_error('Game save failed: %s', error)
                code = 503
                body = {'error': '게임 저장에 실패했습니다. 서버의 저장 공간을 확인한 뒤 다시 불러오세요.'}
        if isinstance(body, dict):
            body = json.dumps(body, ensure_ascii=False).encode('utf-8')
        try:
            self.send_response(code)
            self.send_header('Content-Type', kind)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            self.close_connection = True

    def do_GET(self):
        if self.path not in ('/', '/index.html', '/app.js'):
            self.reply(404, {})
            return
        name = 'app.js' if self.path == '/app.js' else 'index.html'
        kind = 'text/javascript' if name == 'app.js' else 'text/html'
        self.reply(200, (ROOT / 'web' / name).read_bytes(), kind + '; charset=utf-8')

    def do_POST(self):
        origin = self.headers.get('Origin')
        if origin and origin not in ALLOWED_ORIGINS:
            self.reply(403, {'error': '허용되지 않은 출처입니다.'})
            return
        paths = ('/api/start', '/api/state', '/api/guess', '/api/rooms/create', '/api/rooms/join', '/api/rooms/status', '/api/profile')
        if self.path not in paths:
            self.reply(404, {})
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 1024:
                raise ValueError('요청 크기가 잘못되었습니다.')
            data = json.loads(self.rfile.read(length))
            if not isinstance(data, dict):
                raise ValueError('JSON 객체가 필요합니다.')
            name = None
            if 'name' in data:
                if not isinstance(data['name'], str):
                    raise ValueError('닉네임은 문자열이어야 합니다.')
                name = data['name'].strip()
                if not 1 <= len(name) <= 12 or any(not c.isprintable() for c in name):
                    raise ValueError('닉네임은 표시 가능한 문자 1~12자로 입력해주세요.')
            guess = None
            code = None
            if self.path == '/api/guess':
                word = data.get('word')
                if not isinstance(word, str):
                    raise ValueError('단어를 입력해주세요.')
                guess = word.strip().encode('utf-8')
                if not guess or len(guess) >= 64 or b'\x00' in guess:
                    raise ValueError('단어는 UTF-8 기준 1~63바이트여야 합니다.')
            if self.path == '/api/rooms/join':
                code = data.get('code')
                if not isinstance(code, str):
                    raise ValueError('방 코드를 입력해주세요.')
                code = code.strip().upper()
                if len(code) != 6 or any(c not in string.ascii_uppercase + string.digits for c in code):
                    raise ValueError('방 코드는 영문과 숫자 6자리입니다.')
            expire_sessions()
            token = self.headers.get('X-Mallangi-Session', '')
            if self.path == '/api/state':
                if token not in sessions or 'state' not in sessions[token]:
                    self.reply(409, {'error': '저장된 게임이 없거나 만료되었습니다.'})
                    return
                sessions[token]['touched'] = time.time()
                self.reply(200, session_view(token, 'resumed'))
                return
            if self.path == '/api/profile':
                if token not in sessions:
                    self.reply(409, {'error': '게임이 만료되었습니다. 다시 시작해주세요.'})
                    return
                if name is None:
                    raise ValueError('닉네임을 입력해주세요.')
                sessions[token]['name'] = name
                sessions[token]['touched'] = time.time()
                self.reply(200, {'sessionId': token, 'name': name})
                return
            if self.path == '/api/rooms/status':
                session = sessions.get(token)
                if session is None:
                    self.reply(409, {'error': '게임이 만료되었습니다. 다시 시작해주세요.'})
                    return
                room_code = session.get('room')
                if room_code not in rooms:
                    self.reply(409, {'error': '현재 참가 중인 방이 없습니다.'})
                    return
                session['touched'] = time.time()
                players = []
                for member in rooms[room_code]['members']:
                    player = sessions[member]
                    players.append({
                        'number': player['number'],
                        'name': player.get('name', f"플레이어 {player['number']}"),
                        'isMe': member == token,
                        **player['public'],
                    })
                rank_players(players)
                self.reply(200, {
                    'sessionId': token, 'roomCode': room_code,
                    'players': players, 'maxPlayers': MAX_PLAYERS,
                    'allFinished': all(player['finished'] for player in players),
                })
                return
            if code is not None:
                if code not in rooms:
                    self.reply(404, {'error': '방이 없거나 만료되었습니다.'})
                    return
                if token in rooms[code]['members']:
                    self.reply(409, {'error': '이미 참가 중인 방입니다.'})
                    return
                if len(rooms[code]['members']) >= MAX_PLAYERS:
                    self.reply(409, {'error': '방 인원이 가득 찼습니다. 최대 8명입니다.'})
                    return
            if token not in sessions:
                if guess is not None:
                    self.reply(409, {'error': '게임이 만료되었습니다. 다시 시작해주세요.'})
                    return
                if len(sessions) >= MAX_SESSIONS:
                    self.reply(503, {'error': '진행 중인 게임이 많습니다. 잠시 후 시도해주세요.'})
                    return
                handle = engine.mallangi_create()
                if not handle:
                    self.reply(500, {'error': '게임 생성에 실패했습니다.'})
                    return
                token = secrets.token_urlsafe(32)
                sessions[token] = {'handle': handle, 'touched': time.time()}
            session = sessions[token]
            new_room = None
            if self.path == '/api/rooms/create':
                template = engine.mallangi_create()
                if not template:
                    self.reply(500, {'error': '방 생성에 실패했습니다.'})
                    return
                initial = json.loads(engine.mallangi_request(template, None))
                if 'error' in initial:
                    engine.mallangi_destroy(template)
                    self.reply(500, initial)
                    return
                code = ''.join(secrets.choice(string.ascii_uppercase + string.digits) for _ in range(6))
                while code in rooms:
                    code = ''.join(secrets.choice(string.ascii_uppercase + string.digits) for _ in range(6))
                new_room = {'handle': template, 'members': set(), 'nextNumber': 1}
            if code is not None:
                room = new_room if new_room is not None else rooms[code]
                result = json.loads(engine.mallangi_join(session['handle'], room['handle']))
            else:
                result = json.loads(engine.mallangi_request(session['handle'], guess))
            if 'error' in result:
                if new_room is not None:
                    engine.mallangi_destroy(new_room['handle'])
                self.reply(500, result)
                return
            if guess is None:
                session['history'] = []
                leave_room(token)
                if code is not None:
                    if new_room is not None:
                        rooms[code] = new_room
                    rooms[code]['members'].add(token)
                    session['room'] = code
                    session['number'] = rooms[code]['nextNumber']
                    rooms[code]['nextNumber'] += 1
            session['touched'] = time.time()
            if name is not None:
                session['name'] = name
            if guess is not None and result['status'] in ('wrong', 'correct'):
                session['history'].append({
                    'word': guess.decode('utf-8'),
                    'score': 1.0 if result['status'] == 'correct' else result['score'],
                    'status': result['status'],
                })
            session['state'] = result
            # Explicit public fields only: never publish another player's answer,
            # cards, submitted words, or session token through the room API.
            session['public'] = {
                'remaining': result['remaining'],
                'finished': result['finished'],
                'won': result['won'],
                # C consumes an attempt only for wrong answers. Include the
                # winning submission once; duplicates/unknowns never count.
                'attemptsUsed': MAX_ATTEMPTS - result['remaining'] + int(result['won']),
            }
            self.reply(200, session_view(token))
        except (ValueError, UnicodeError) as error:
            self.reply(400, {'error': str(error)})


if __name__ == '__main__':
    # Do not silently replace corrupt/incompatible saves with an empty game.
    print('저장된 게임을 확인하고 있습니다...', flush=True)
    sessions, rooms = storage.load(STATE_FILE, engine, TTL, MAX_SESSIONS, MAX_PLAYERS)
    with HTTPServer((BIND_HOST, 8000), Handler) as server:
        print(f'MALLANGI listening on {BIND_HOST}:8000 (Ctrl+C to stop)', flush=True)
        print('Allowed browser addresses: ' + ', '.join(sorted(ALLOWED_ORIGINS)), flush=True)
        print(f'Restored {len(sessions)} games and {len(rooms)} rooms. Save: {STATE_FILE}', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print('\nMALLANGI 서버를 종료했습니다.')
        finally:
            for session in sessions.values():
                engine.mallangi_destroy(session['handle'])
            for room in rooms.values():
                engine.mallangi_destroy(room['handle'])

