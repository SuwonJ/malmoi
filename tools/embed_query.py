"""Encode text with the exact E5 ONNX assets used by a prepared MLM (CPU, offline)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def fingerprint(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
    return digest.hexdigest()


def space_from_mlm(path):
    with Path(path).open('rb') as stream:
        header=stream.read(40)
        if len(header)!=40 or header[:8]!=b'MALMOI\0\0':raise ValueError('Invalid MLM')
        for _ in range(2):
            prefix=stream.read(4)
            if len(prefix)!=4:raise ValueError('Truncated MLM descriptor')
            length,=struct.unpack('<I',prefix)
            if length>65536:raise ValueError('Descriptor too long')
            raw=stream.read(length)
            if len(raw)!=length:raise ValueError('Truncated MLM descriptor')
        space=json.loads(raw.decode('utf-8'))
        if space.get('dimension')!=struct.unpack_from('<I',header,24)[0]:raise ValueError('Dimension mismatch')
        return space


class QueryEncoder:
    def __init__(self,database,model,threads=4):
        if threads<1:raise ValueError('threads must be positive')
        model=Path(model)
        self.space=space_from_mlm(database)
        info=self.space
        download=json.loads((model/'download.json').read_text(encoding='utf-8'))
        if (info.get('model_id')!=download['repo'] or info.get('revision')!=download['revision']
            or info.get('query_prefix')!='query: ' or info.get('document_prefix')!='passage: '
            or info.get('pooling')!='attention-mask-mean' or info.get('normalization')!='l2'
            or info.get('max_length')!=128 or info.get('dtype')!='float32'
            or info.get('representation')!='dictionary-definition'):
            raise ValueError('Unsupported or mismatched E5 space')
        for key,name in [('onnx_sha256','onnx/model_quantized.onnx'),('tokenizer_sha256','tokenizer.json')]:
            if fingerprint(model/name)!=info.get(key):raise ValueError('Model asset hash mismatch: '+name)
        import numpy as np
        import onnxruntime as ort
        from tokenizers import Tokenizer
        self.np=np
        self.tokenizer=Tokenizer.from_file(str(model/'tokenizer.json'))
        self.tokenizer.enable_truncation(max_length=128)
        self.tokenizer.enable_padding(pad_id=1,pad_token='<pad>')
        options=ort.SessionOptions();options.intra_op_num_threads=threads;options.inter_op_num_threads=1
        self.session=ort.InferenceSession(str(model/'onnx/model_quantized.onnx'),sess_options=options,providers=['CPUExecutionProvider'])
        self.inputs={item.name for item in self.session.get_inputs()}

    def encode(self,text):
        if not isinstance(text,str) or not text.strip():raise ValueError('Text must not be empty')
        if len(text.encode('utf-8'))>8192:raise ValueError('Text exceeds 8192 UTF-8 bytes')
        np=self.np
        tokens=self.tokenizer.encode_batch(['query: '+text])
        mask=np.asarray([item.attention_mask for item in tokens],dtype=np.int64)
        feed={'input_ids':np.asarray([item.ids for item in tokens],dtype=np.int64),
              'attention_mask':mask,'token_type_ids':np.asarray([item.type_ids for item in tokens],dtype=np.int64)}
        hidden=self.session.run(None,{key:value for key,value in feed.items() if key in self.inputs})[0]
        pooled=(hidden*mask[...,None]).sum(axis=1)/mask.sum(axis=1)[:,None]
        pooled/=np.maximum(np.linalg.norm(pooled,axis=1,keepdims=True),1e-12)
        vector=pooled[0].astype(np.float32)
        if vector.shape!=(self.space['dimension'],) or not np.isfinite(vector).all():raise ValueError('Unexpected embedding output')
        return vector.tolist()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database',type=Path,required=True)
    parser.add_argument('--model',type=Path,required=True)
    parser.add_argument('--text',required=True)
    parser.add_argument('--output',type=Path,help='Output JSON, stdout if omitted')
    parser.add_argument('--threads',type=int,default=4)
    args=parser.parse_args()
    try:
        if args.output:
            target=args.output.resolve()
            if target in {args.database.resolve(),Path(str(args.database)+'.json').resolve()} or target==args.model.resolve() or args.model.resolve() in target.parents:
                raise ValueError('Output must not overwrite dictionary or model assets')
        encoder=QueryEncoder(args.database,args.model,args.threads)
        result={'text':args.text,'dimension':encoder.space['dimension'],'space':encoder.space,
                'space_json':json.dumps(encoder.space,ensure_ascii=False,sort_keys=True,separators=(',',':')),
                'vector':encoder.encode(args.text)}
        payload=json.dumps(result,ensure_ascii=False,allow_nan=False)+'\n'
        if args.output:args.output.write_text(payload,encoding='utf-8')
        else:print(payload,end='')
    except (OSError,ValueError,KeyError,ImportError) as error:parser.error(str(error))


if __name__=='__main__':main()
