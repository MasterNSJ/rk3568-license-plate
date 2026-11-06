"""Replay actual board OCR outputs through the production C++ consensus implementation.
Frames are derived from public photographs, not independently captured road videos.
"""
import argparse,json,subprocess
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('manifest',type=Path);p.add_argument('results',type=Path)
 p.add_argument('--replay',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
 a=p.parse_args();manifest=json.loads(a.manifest.read_text(encoding='utf-8'));items={x['id']:x for x in manifest}
 a.output.mkdir(parents=True,exist_ok=True);summary=[]
 variants=['original','dim','original','blur','bright','original','rotate5','original','dim','original']
 for source in range(1,8):
  lines=[];inputs=[];truth=items[f'p{source:02d}_original']['plates'][0]['text']
  for index,variant in enumerate(variants):
   ident=f'p{source:02d}_{variant}'
   result=json.loads((a.results/ident/'results.json').read_text(encoding='utf-8'))
   admitted=[d for d in result['detections'] if d.get('accepted',True)]
   if len(admitted)!=1:
    lines.append(f'{index*1000} 0 0 0 0 -1 -\n');raw='';valid=False
   else:
    d=admitted[0];x1,y1,x2,y2=d['bbox_xyxy'];raw=d['raw_plate']
    valid=d.get('basic_complete',False) and not d.get('review_reason')
    lines.append(f'{index*1000} {x1} {y1} {x2-x1} {y2-y1} {int(valid)} {raw or "-"}\n')
   inputs.append({'timestamp_ms':index*1000,'frame':ident,'raw':raw,'valid':valid,'truth':truth})
  # No subsequent result callbacks: exercise the independent expiry clock.
  lines += [f'{t} 0 0 0 0 0 -\n' for t in (10000,11000,12000,13000)]
  for window in (1,3,5):
   result=subprocess.run([str(a.replay.resolve()),str(window)],input=''.join(lines),text=True,
                         encoding='utf-8',capture_output=True,check=True)
   records=[line.split('\t') for line in result.stdout.splitlines()]
   confirmed=[r for r in records[:10] if r[2]!='-']
   visible=[r[2] for r in confirmed]
   correct_times=[int(r[0]) for r in confirmed if r[2]==truth]
   row={'source':source,'window':window,'truth':truth,'frames':10,
        'confirmed_frames':len(confirmed),'wrong_confirmations':sum(r[2]!=truth for r in confirmed),
        'text_changes_between_confirmed_outputs':sum(x!=y for x,y in zip(visible,visible[1:])),
        'first_correct_confirmation_ms':correct_times[0] if correct_times else None,
        'state_cleared_by_12000ms':records[12][2]=='-'}
   summary.append(row)
   (a.output/f'source{source}_w{window}.json').write_text(json.dumps({'metrics':row,'input':inputs,'output_tsv':result.stdout},ensure_ascii=False,indent=2),encoding='utf-8')
 aggregate=[]
 for window in (1,3,5):
  rows=[r for r in summary if r['window']==window]
  aggregate.append({'window':window,'frames':sum(r['frames'] for r in rows),
                    'confirmed_frames':sum(r['confirmed_frames'] for r in rows),
                    'wrong_confirmations':sum(r['wrong_confirmations'] for r in rows),
                    'text_changes':sum(r['text_changes_between_confirmed_outputs'] for r in rows),
                    'expiry_checks_passed':sum(r['state_cleared_by_12000ms'] for r in rows)})
 report={'kind':'board_OCR_outputs_photo_derived_sequence_replay','config_results':str(a.results),
         'independent_sources':7,'sequence_frames_per_source':10,'aggregate':aggregate,'sequences':summary,
         'limitations':['Uses real board model outputs but controlled photo-derived frames, not recorded road footage',
                        'Reused frame outputs are deterministic replay; no new NPU timing measurements here',
                        'Logical timestamps are 1000ms apart; confirmation delay is not processing latency']}
 (a.output/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
 print(json.dumps(aggregate,ensure_ascii=False))
if __name__=='__main__':main()
