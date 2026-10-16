"""Copy only the 15 acceptance inputs into a new directory and verify SHA256."""
import argparse
import csv
import hashlib
import pathlib
import shutil

parser=argparse.ArgumentParser()
parser.add_argument('--repository-root',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[3])
parser.add_argument('--flat-input',type=pathlib.Path,help='Existing flat directory instead of repository dataset paths')
parser.add_argument('--output',type=pathlib.Path,required=True)
args=parser.parse_args()
manifest=pathlib.Path(__file__).with_name('image_manifest.csv')
with manifest.open(encoding='utf-8',newline='') as f:rows=list(csv.DictReader(f))
assert len(rows)==15
sources=[]
for row in rows:
    source=(args.flat_input/row['filename']) if args.flat_input else (args.repository_root/row['repository_path'])
    if hashlib.sha256(source.read_bytes()).hexdigest()!=row['sha256']:raise ValueError('SHA256 mismatch: '+row['filename'])
    sources.append(source)
args.output.mkdir(exist_ok=False)
for row,source in zip(rows,sources):shutil.copy2(source,args.output/row['filename'])
(args.output/'images.txt').write_text(''.join(r['filename']+'\n' for r in rows),encoding='utf-8')
shutil.copy2(manifest,args.output/'image_manifest.csv')
print('PASS: copied and verified 15 images; original inputs unchanged')
