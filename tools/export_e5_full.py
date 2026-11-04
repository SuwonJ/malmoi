"""Stream all verified E5 definition vectors to MLM v1 using bounded-memory SQLite."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import struct
import tempfile
from export_e5 import fingerprint
from prepare import HEADER, MAGIC, MAX_FILE_BYTES, canonical, json_text, text


def export_all(dictionary, cache, manifest_path, model, output):
    import numpy as np
    dictionary,cache,manifest_path,model,output=map(Path,(dictionary,cache,manifest_path,model,output))
    protected={dictionary.resolve(),cache.resolve(),cache.with_suffix('.json').resolve(),manifest_path.resolve()}
    protected.update(p.resolve() for p in model.rglob('*') if p.is_file())
    if {output.resolve(),Path(str(output)+'.json').resolve()} & protected:
        raise ValueError('Output must not overwrite input assets')
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    state=json.loads(cache.with_suffix('.json').read_text(encoding='utf-8'))
    source_hash=fingerprint(dictionary)
    cache_hash=fingerprint(cache)
    download=json.loads((model/'download.json').read_text(encoding='utf-8'))
    model_hash=fingerprint(model/'onnx/model_quantized.onnx')
    tokenizer_hash=fingerprint(model/'tokenizer.json')
    if (manifest['status']!='completed' or source_hash!=manifest['data_sha256']
        or source_hash!=state['data_sha256'] or state['completed']!=state['total']
        or cache_hash!=manifest['embedding_sha256'] or model_hash!=manifest['model_sha256']
        or model_hash!=state['model_sha256'] or tokenizer_hash!=manifest['tokenizer_sha256']
        or download!=manifest['model_download']):
        raise ValueError('Dictionary/cache/model provenance or completion mismatch')
    vectors=np.load(cache,mmap_mode='r',allow_pickle=False)
    if vectors.ndim!=2 or vectors.shape[0]!=state['total'] or vectors.dtype!=np.dtype('float32'):
        raise ValueError('Cache shape/dtype mismatch')
    dimension=int(vectors.shape[1])
    if not 1<=dimension<=65536:raise ValueError('Invalid vector dimension')
    info=dict(model_id=download['repo'],revision=download['revision'],dimension=dimension,
              dtype='float32',pooling='attention-mask-mean',normalization='l2',query_prefix='query: ',
              document_prefix='passage: ',max_length=128,onnx_sha256=model_hash,
              tokenizer_sha256=tokenizer_hash,representation='dictionary-definition')
    output.parent.mkdir(parents=True,exist_ok=True)
    temporary=None
    with tempfile.TemporaryDirectory(prefix='mlm-export-',dir=output.parent) as work:
        connection=sqlite3.connect(str(Path(work)/'mapping.sqlite'))
        try:
            connection.executescript('''PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;
            PRAGMA cache_size=-16384; PRAGMA temp_store=FILE;
            CREATE TABLE definitions(definition TEXT PRIMARY KEY, row_id INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE entries(source TEXT PRIMARY KEY,word TEXT,key TEXT,definition TEXT,pos TEXT,row_id INTEGER) WITHOUT ROWID;''')
            unique=count=source_rows=duplicates=0
            with dictionary.open(encoding='utf-8') as stream:
                for line in stream:
                    row=json.loads(line)
                    if row.get('word_type')!='고유어' or not row.get('word') or not row.get('definition'):continue
                    if row.get('lexeme_id') is not None:
                        raise ValueError('Full source-sense exporter requires dictionary without explicit lexeme_id')
                    word=text(row['word']);definition=text(row['definition']);pos=text(row.get('pos') or '',empty=True)
                    if type(row.get('id')) not in (str,int):raise ValueError('Missing source sense ID')
                    source=text(str(row['id']))
                    found=connection.execute('SELECT row_id FROM definitions WHERE definition=?',(definition,)).fetchone()
                    if found is None:
                        index=unique;unique+=1
                        connection.execute('INSERT INTO definitions VALUES (?,?)',(definition,index))
                    else:index=found[0]
                    cursor=connection.execute('INSERT OR IGNORE INTO entries VALUES (?,?,?,?,?,?)',(source,word,canonical(word),definition,pos,index))
                    source_rows+=1
                    if cursor.rowcount:count+=1
                    else:
                        previous=connection.execute('SELECT word,definition,pos FROM entries WHERE source=?',(source,)).fetchone()
                        if previous!=(word,definition,pos):raise ValueError('Conflicting source sense ID: '+source)
                        duplicates+=1
                    if source_rows%50000==0:print(f'Indexed {source_rows:,} source rows',flush=True)
            connection.commit()
            if unique!=len(vectors) or source_rows!=manifest['senses']:
                raise ValueError('Source definition mapping does not match completed cache')
            keys=connection.execute('SELECT COUNT(DISTINCT key) FROM entries').fetchone()[0]
            metadata=dict(dataset_version=f'e5-definitions-{count}-v1',format_version=1,prepare_version=1,
                          word_count=count,sense_count=count,synthetic=False,source_sha256=source_hash,
                          cache_sha256=cache_hash,source_senses=source_rows,identical_source_duplicates=duplicates,selected_keys=keys,
                          key_policy='NFC; remove ASCII - and ^; preserve raw spelling',
                          id_policy='sorted source sense keys; IDs local to dataset version',
                          selection='all eligible unique source senses; identical source duplicates collapsed; deduplicated cache mapping')
            space=json_text(info)
            def packed_string(value):
                raw=value.encode('utf-8');return struct.pack('<I',len(raw))+raw
            prefix=HEADER.pack(MAGIC,1,40,count,count,dimension,0,1,0)
            prefix+=packed_string(json_text(metadata))+packed_string(space)+packed_string('')
            # Exact byte count before writing the large artifact.
            variable=0
            for source,word,key,definition,pos in connection.execute('SELECT source,word,key,definition,pos FROM entries'):
                variable+=sum(len(v.encode('utf-8')) for v in (word,key,'sense:'+source,source,definition,pos))+28+dimension*4
            expected=len(prefix)+variable
            print(f'Output: {count:,} senses; {keys:,} normalized keys; {expected:,} bytes',flush=True)
            if expected>MAX_FILE_BYTES:raise ValueError(f'Full MLM needs {expected} bytes, exceeding v1 limit {MAX_FILE_BYTES}')
            digest=hashlib.sha256();written=0
            with tempfile.NamedTemporaryFile('wb',dir=output.parent,delete=False) as stream:
                temporary=stream.name
                def write(payload):
                    nonlocal written
                    stream.write(payload);digest.update(payload);written+=len(payload)
                write(prefix)
                for source,word,key,index in connection.execute('SELECT source,word,key,row_id FROM entries ORDER BY source'):
                    vector=vectors[index]
                    if not np.isfinite(vector).all() or abs(float(np.dot(vector,vector))-1)>.0002:
                        raise ValueError('Invalid/non-unit cache vector')
                    write(packed_string(word)+packed_string(key)+packed_string('sense:'+source))
                    write(vector.astype('<f4',copy=False).tobytes())
                for identifier,(source,definition,pos) in enumerate(connection.execute('SELECT source,definition,pos FROM entries ORDER BY source')):
                    write(struct.pack('<I',identifier)+packed_string(source)+packed_string(definition)+packed_string(pos))
            if written!=expected:raise ValueError('Byte count mismatch')
            os.replace(temporary,output);temporary=None
            result={**metadata,'file_sha256':digest.hexdigest(),'bytes':written,'word_space':space,'sense_space':''}
            from prepare import atomic_write
            atomic_write(str(output)+'.json',(json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
            print(json.dumps({k:result[k] for k in ('word_count','sense_count','selected_keys','bytes','file_sha256')},ensure_ascii=False))
            return result
        finally:
            connection.close()
            if temporary is not None:Path(temporary).unlink(missing_ok=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('dictionary','cache','manifest','model','output'):parser.add_argument('--'+key,type=Path,required=True)
    args=parser.parse_args()
    try:export_all(args.dictionary,args.cache,args.manifest,args.model,args.output)
    except (OSError,ValueError,KeyError,ImportError,sqlite3.Error) as error:parser.error(str(error))


if __name__=='__main__':main()
