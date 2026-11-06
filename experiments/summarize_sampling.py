import argparse, json, statistics
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);a=p.parse_args();rows=[]
    for f in sorted(a.directory.glob('metrics_*.jsonl')):
        events=[json.loads(line) for line in f.read_text(encoding='utf-8').splitlines() if line.strip()]
        results=[e for e in events if e.get('event')=='result_submitted' and 'capture_to_ui_submit_ms' in e]
        def summary(key):
            v=sorted(float(r[key]) for r in results if key in r)
            return {'count':len(v),'median_ms':statistics.median(v) if v else None,
                    'p95_ms':v[max(0,__import__('math').ceil(.95*len(v))-1)] if v else None}
        rows.append({'source':f.name,'completed_requests':len(results),
                     'capture_to_ui_submit':summary('capture_to_ui_submit_ms'),
                     'convert':summary('frame_convert_ms'),'encode':summary('frame_encode_ms')})
    print(json.dumps({'kind':'board_camera_metrics','runs':rows,'scope':'UI state submission, not physical display latency'},indent=2))
if __name__=='__main__':main()
