"""Publish numeric camera telemetry, excluding camera pictures and recognized strings."""
import argparse,csv,json,math,re,statistics
from pathlib import Path

def stats(values):
 v=sorted(values)
 return {'n':len(v),'median':statistics.median(v) if v else None,
         'p95':v[max(0,math.ceil(.95*len(v))-1)] if v else None}
def main():
 p=argparse.ArgumentParser();p.add_argument('--png',required=True,type=Path);p.add_argument('--bmp',required=True,type=Path)
 p.add_argument('--output',required=True,type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 rows=[]
 for format,folder in [('png',a.png),('bmp',a.bmp)]:
  for interval in (1000,500,250):
   file=folder/f'metrics_{interval}.jsonl'
   events=[json.loads(line) for line in file.read_text(encoding='utf-8').splitlines() if line.strip()]
   results=[r for r in events if r.get('event')=='result_submitted' and 'capture_to_ui_submit_ms' in r]
   allowed=['event','request_id','model_calls_ms','request_to_ui_submit_ms','sample_interval_ms',
            'frame_convert_ms','frame_encode_ms','capture_to_ui_submit_ms','frame_format','frame_file_bytes']
   sanitized=[{key:r[key] for key in allowed if key in r} for r in results]
   (a.output/f'{format}_{interval}.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in sanitized),encoding='utf-8')
   row={'format':format,'interval_ms':interval,'nominal_duration_seconds':60,'completed_requests':len(results),
        'completed_requests_per_second':len(results)/60}
   for label,key in [('e2e','capture_to_ui_submit_ms'),('encode','frame_encode_ms'),
                     ('convert','frame_convert_ms'),('model','model_calls_ms'),('bytes','frame_file_bytes')]:
    desc=stats([r[key] for r in results if key in r]);row[label+'_median']=desc['median'];row[label+'_p95']=desc['p95']
   resources=(folder/f'resources_{interval}.txt').read_text(encoding='utf-8')
   rss=[int(v) for v in re.findall(r'VmRSS:\s*(\d+)',resources)]
   # Older harnesses could observe a wrapper shell rather than the Qt PID.
   verified='observed_pid=' in resources
   row.update(rss_min_kb=min(rss) if rss and verified else None,rss_max_kb=max(rss) if rss and verified else None,
              resource_pid_verified=verified)
   rows.append(row)
 with (a.output/'comparison.csv').open('w',encoding='utf-8',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
 report={'kind':'RK3568_live_camera_pipeline_measurements','rows':rows,
         'scope':'Frame arrival selection to UI state submission; not physical display latency',
         'input':'Unlabeled live camera scene, no real plate accuracy claim',
         'limitations':['PNG runs precede BMP runs; scene, temperature and ordering can affect results',
                        '60 second trials include startup, first model initialization and shutdown',
                        'Changing encoding does not make NPU inference itself faster',
                        'BMP frames are about 9.9 MB; storage traffic must be considered',
                        'Wrapper-shell resource samples from earlier runs are excluded']}
 (a.output/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
 print(json.dumps(rows,indent=2))
if __name__=='__main__':main()
