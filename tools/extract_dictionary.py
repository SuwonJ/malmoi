"""Extract usable word/definition records from local Urimalsaem JSON files (stdlib)."""
import argparse
import json
import os
from pathlib import Path
import tempfile


def extract(source, output):
    paths = sorted(Path(source).glob('*.json'))
    output = Path(output)
    if not paths:
        raise ValueError('No JSON dictionary files in source directory')
    if output.resolve() in {path.resolve() for path in paths}:
        raise ValueError('Output must not overwrite an input')
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    count = 0
    try:
        with tempfile.NamedTemporaryFile('w', encoding='utf-8', dir=output.parent, delete=False) as stream:
            temporary = stream.name
            for path in paths:
                with path.open(encoding='utf-8-sig') as input_stream:
                    data = json.load(input_stream)
                for item in data['channel']['item']:
                    word_info = item.get('wordinfo') or {}
                    sense_info = item.get('senseinfo') or {}
                    word, definition = word_info.get('word'), sense_info.get('definition')
                    if not word or not definition:
                        continue
                    if not isinstance(word, str) or not isinstance(definition, str):
                        raise ValueError('Word and definition must be strings')
                    identifier = item.get('target_code')
                    if type(identifier) not in (str, int):
                        raise ValueError('Usable records must have target_code')
                    row = dict(id=identifier, word=word, definition=definition,
                               word_type=word_info.get('word_type'), pos=sense_info.get('pos'),
                               sense_type=sense_info.get('type'))
                    stream.write(json.dumps(row, ensure_ascii=False) + '\n')
                    count += 1
        if not count:
            raise ValueError('No usable word/definition records')
        os.replace(temporary, output)
        temporary = None
    finally:
        if temporary is not None:
            Path(temporary).unlink(missing_ok=True)
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(f'{extract(args.source, args.output)} records -> {args.output}')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
