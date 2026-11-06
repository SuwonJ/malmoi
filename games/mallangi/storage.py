"""Transactional SQLite snapshots; C pointers are never written to disk."""
import ctypes
from contextlib import closing
import json
import os
from pathlib import Path
import sqlite3
import time

VERSION = 3


def save(path, engine, sessions, rooms):
    def game(handle, cards):
        return {'answer': engine.mallangi_saved_answer(handle).decode('utf-8'),
                'has_sense':bool(engine.mallangi_saved_has_sense(handle)),
                'sense_id':int(engine.mallangi_saved_sense_id(handle)),
                'cards': cards}

    saved_sessions = {}
    for token, session in sessions.items():
        if 'state' not in session:
            continue
        saved = {key: value for key, value in session.items() if key != 'handle'}
        saved['game'] = game(session['handle'], session['state']['cards'])
        saved_sessions[token] = saved
    saved_rooms = {}
    for code, room in rooms.items():
        if not any(token in saved_sessions for token in room['members']):
            continue
        state = json.loads(engine.mallangi_request(room['handle'], b''))
        if 'error' in state:
            raise ValueError(state['error'])
        saved_rooms[code] = {
            'nextNumber': room['nextNumber'],
            'game': game(room['handle'], state['cards']),
        }
    payload = json.dumps({'version': VERSION, 'sessions': saved_sessions,
                          'rooms': saved_rooms}, ensure_ascii=False, allow_nan=False)
    # ponytail: bounded 64-session snapshot; use per-session rows if throughput grows.
    path = Path(path)
    with closing(sqlite3.connect(path, timeout=5)) as database, database:
        database.execute('CREATE TABLE IF NOT EXISTS snapshot (id INTEGER PRIMARY KEY CHECK(id=1), payload TEXT NOT NULL)')
        database.execute('INSERT OR REPLACE INTO snapshot VALUES (1, ?)', (payload,))
    os.chmod(path, 0o600)


def load(path, engine, ttl, max_sessions, max_players):
    path = Path(path)
    if not path.exists():
        return {}, {}
    with closing(sqlite3.connect(path, timeout=5)) as database, database:
        row = database.execute('SELECT payload FROM snapshot WHERE id=1').fetchone()
    if row is None:
        raise ValueError('Saved game snapshot is missing; preserve the file and investigate.')
    data = json.loads(row[0])
    if data.get('version') != VERSION:
        raise ValueError('Unsupported saved game version or score model; preserve the old snapshot and start a separate new game')
    if len(data['sessions']) > max_sessions or len(data['rooms']) > max_sessions:
        raise ValueError('Saved game capacity exceeded')
    allocated = []

    def restore(game):
        cards = game['cards']
        if len(cards) != 3:
            raise ValueError('Invalid saved cards')
        words = (ctypes.c_char_p * len(cards))(*(card['word'].encode('utf-8') for card in cards))
        scores = (ctypes.c_float * len(cards))(*(card['score'] for card in cards))
        answer = game['answer'].encode('utf-8')
        if b'\x00' in answer or any(b'\x00' in word for word in words):
            raise ValueError('Invalid saved word')
        handle = engine.mallangi_create()
        if not handle:
            raise MemoryError('Game allocation failed')
        allocated.append(handle)
        if type(game.get('has_sense')) is not bool or type(game.get('sense_id')) is not int or not -(2**63)<=game['sense_id']<2**63:
            raise ValueError('Saved answer sense is missing or invalid')
        if not engine.mallangi_restore_sense(handle, answer, int(game['has_sense']), game['sense_id'], words, scores, len(cards)):
            raise ValueError('Cannot restore cards/answer: check that the dataset has not changed')
        return handle

    sessions, rooms = {}, {}
    try:
        for token, saved in data['sessions'].items():
            if time.time() - saved['touched'] > ttl:
                continue
            if not 0 <= len(saved['history']) <= 5:
                raise ValueError('Invalid saved history')
            game = saved.pop('game')
            handle = restore(game)
            for item in saved['history']:
                result = json.loads(engine.mallangi_request(handle, item['word'].encode('utf-8')))
                if result.get('status') != item['status'] or item['status'] not in ('correct', 'wrong'):
                    raise ValueError('Saved history cannot be replayed')
                if item['status'] == 'wrong' and abs(result['score'] - item['score']) > 0.0001:
                    raise ValueError('Dataset scores changed; refusing to overwrite saved games')
            current = json.loads(engine.mallangi_request(handle, b''))
            for key in ('remaining', 'finished', 'won', 'cards'):
                if current[key] != saved['state'][key]:
                    raise ValueError(f'Saved state mismatch: {key}')
            if current.get('answerSenseId') != saved['state'].get('answerSenseId'):
                raise ValueError('Saved answer sense mismatch')
            if current.get('answer') != saved['state'].get('answer'):
                raise ValueError('Saved answer mismatch')
            # Preserve the last response's score/status as well as the verified
            # C state; a read-only C snapshot has no last-submission score.
            saved['handle'] = handle
            sessions[token] = saved
        for code, saved in data['rooms'].items():
            members = {token for token, session in sessions.items() if session.get('room') == code}
            if not members:
                continue
            if len(members) > max_players:
                raise ValueError('Saved room capacity exceeded')
            handle = restore(saved['game'])
            if any(engine.mallangi_saved_answer(sessions[token]['handle']).decode('utf-8') != saved['game']['answer'] or bool(engine.mallangi_saved_has_sense(sessions[token]['handle'])) != saved['game']['has_sense'] or int(engine.mallangi_saved_sense_id(sessions[token]['handle'])) != saved['game']['sense_id'] for token in members):
                raise ValueError('Saved room answer mismatch')
            rooms[code] = {'handle': handle, 'members': members, 'nextNumber': saved['nextNumber']}
        if any(session.get('room') and session['room'] not in rooms for session in sessions.values()):
            raise ValueError('Saved room reference is missing')
        return sessions, rooms
    except BaseException:
        for handle in allocated:
            engine.mallangi_destroy(handle)
        raise
