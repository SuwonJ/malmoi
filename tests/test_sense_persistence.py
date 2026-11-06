"""Optional real native bridge test. Uses an isolated temporary SQLite file."""
import ctypes
from contextlib import closing
import importlib.util
import json
import os
from pathlib import Path
import sqlite3
import tempfile
import time
import unittest

LIBRARY = os.environ.get('MALMOI_SENSE_BRIDGE')

@unittest.skipUnless(LIBRARY, 'Set MALMOI_SENSE_BRIDGE to test the native room/persistence bridge')
class SensePersistenceTests(unittest.TestCase):
    def test_roundtrip_and_room_preserve_sense(self):
        old = {k: os.environ.get(k) for k in ('MALLANGI_ANSWER','MALLANGI_SENSE_ID','MALMOI_NATIVE_INDEX','MALMOI_KIWI_MODEL')}
        path = Path(LIBRARY).resolve()
        os.environ['MALMOI_NATIVE_INDEX'] = str(path.parent/'runtime/index')
        os.environ['MALMOI_KIWI_MODEL'] = str(path.parent/'runtime/model/models/cong/base')
        lib = ctypes.CDLL(str(path))
        ptr = ctypes.c_void_p
        lib.mallangi_create.restype = ptr
        lib.mallangi_destroy.argtypes = [ptr]
        lib.mallangi_request.argtypes = [ptr, ctypes.c_char_p]
        lib.mallangi_request.restype = ctypes.c_char_p
        lib.mallangi_join.argtypes = [ptr, ptr]
        lib.mallangi_join.restype = ctypes.c_char_p
        lib.mallangi_saved_answer.argtypes = [ptr]
        lib.mallangi_saved_answer.restype = ctypes.c_char_p
        lib.mallangi_saved_sense_id.argtypes = [ptr]
        lib.mallangi_saved_sense_id.restype = ctypes.c_int64
        lib.mallangi_saved_has_sense.argtypes = [ptr]
        lib.mallangi_saved_has_sense.restype = ctypes.c_int
        lib.mallangi_restore_sense.argtypes = [ptr, ctypes.c_char_p, ctypes.c_int, ctypes.c_int64, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_float), ctypes.c_int]
        lib.mallangi_restore_sense.restype = ctypes.c_int
        module = Path(__file__).resolve().parents[1]/'games/mallangi/storage.py'
        spec = importlib.util.spec_from_file_location('sense_storage_test', module)
        storage = importlib.util.module_from_spec(spec); spec.loader.exec_module(storage)
        allocated = []
        try:
            os.environ['MALLANGI_ANSWER'] = '이슬비'; os.environ['MALLANGI_SENSE_ID'] = '250524'
            room = lib.mallangi_create(); allocated.append(room)
            self.assertNotIn('error', json.loads(lib.mallangi_request(room, None)))
            # Room answer sense must override the process's later default selection.
            os.environ['MALLANGI_SENSE_ID'] = '1626980'
            rain = lib.mallangi_create(); allocated.append(rain)
            self.assertNotIn('error', json.loads(lib.mallangi_join(rain, room)))
            self.assertEqual(lib.mallangi_saved_sense_id(rain), 250524)
            song = lib.mallangi_create(); allocated.append(song)
            self.assertNotIn('error', json.loads(lib.mallangi_request(song, None)))
            sessions = {}
            for token, handle, expected in [('rain',rain,.013481345730), ('song',song,1.0)]:
                result=json.loads(lib.mallangi_request(handle, ' 사랑의 그림자 \t'.encode()))
                self.assertEqual(result['status'],'wrong'); self.assertAlmostEqual(result['score'],expected,places=7)
                sessions[token]={'handle':handle,'touched':time.time(),'history':[dict(result,word=' 사랑의 그림자 \t')],'state':result}
            sessions['rain']['room']='ROOM'
            rooms={'ROOM':{'handle':room,'members':{'rain'},'nextNumber':2}}
            with tempfile.TemporaryDirectory() as d:
                file=Path(d)/'state.sqlite3'; storage.save(file,lib,sessions,rooms)
                with closing(sqlite3.connect(file)) as db, db: saved=json.loads(db.execute('SELECT payload FROM snapshot').fetchone()[0])
                self.assertEqual(saved['sessions']['rain']['game']['sense_id'],250524)
                self.assertEqual(saved['sessions']['song']['game']['sense_id'],1626980)
                restored, restored_rooms=storage.load(file,lib,3600,64,8)
                allocated += [s['handle'] for s in restored.values()] + [r['handle'] for r in restored_rooms.values()]
                self.assertEqual(lib.mallangi_saved_sense_id(restored['rain']['handle']),250524)
                self.assertEqual(lib.mallangi_saved_sense_id(restored['song']['handle']),1626980)
                duplicate=json.loads(lib.mallangi_request(restored['rain']['handle'],'사랑의 그림자'.encode()))
                self.assertEqual(duplicate['status'],'duplicate')
                # No word-only migration that could select a different meaning.
                saved['version']=2
                with closing(sqlite3.connect(file)) as db, db: db.execute('UPDATE snapshot SET payload=?',(json.dumps(saved),))
                before=file.read_bytes()
                with self.assertRaisesRegex(ValueError,'score model'): storage.load(file,lib,3600,64,8)
                self.assertEqual(before,file.read_bytes())
        finally:
            for handle in allocated: lib.mallangi_destroy(handle)
            for key,value in old.items():
                if value is None: os.environ.pop(key,None)
                else: os.environ[key]=value

if __name__ == '__main__': unittest.main()
