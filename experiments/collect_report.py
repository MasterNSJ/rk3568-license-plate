import argparse,csv,json,math,statistics
from pathlib import Path
from evaluate_images import evaluate

def describe(values):
 v=sorted(values)
 return {'n':len(v),'median_ms':statistics.median(v) if v else None,
         'p95_ms':v[max(0,math.ceil(.95*len(v))-1)] if v else None}
def main():
 p=argparse.ArgumentParser();p.add_argument('manifest',type=Path);p.add_argument('results',type=Path)
 p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 items=json.loads(a.manifest.read_text(encoding='utf-8'));rows=[];timing=[]
 kinds=sorted({i['kind'] for i in items})
 for folder in sorted(a.results.iterdir()):
  if not (folder/'evaluation.json').exists():continue
  old=json.loads((folder/'evaluation.json').read_text(encoding='utf-8'))
  final=evaluate(items,folder);final.update({k:old[k] for k in ('config','ready_host_ms','request_timings') if k in old})
  final['by_input_kind']={kind:evaluate([i for i in items if i['kind']==kind],folder)['metrics'] for kind in kinds}
  variants=sorted({i['variant'] for i in items if i['plates']})
  final['by_positive_variant']={variant:evaluate([i for i in items if i['plates'] and i['variant']==variant],folder)['metrics'] for variant in variants}
  (folder/'evaluation.json').write_text(json.dumps(final,indent=2,ensure_ascii=False),encoding='utf-8')
  (a.output/(folder.name+'_evaluation.json')).write_text(json.dumps(final,indent=2,ensure_ascii=False),encoding='utf-8')
  rows.append({'configuration':folder.name,**final['metrics']})
  measurements=[]
  for item in items:
   result=json.loads((folder/item['id']/'results.json').read_text(encoding='utf-8'))
   other=json.loads((folder/item['id']/'timing.json').read_text(encoding='utf-8'))
   row={'configuration':folder.name,'id':item['id'],'kind':item['kind'],
        'preprocess_ms':result.get('preprocess_ms',0),'detection_call_ms':result['detection_call_ms'],
        'postprocess_ms':result.get('postprocess_ms',0),'engine_total_ms':result.get('engine_total_ms',0),
        'crop_ms':sum(d.get('crop_ms',0) for d in result['detections']),
        'recognition_call_ms':sum(d.get('recognition_call_ms',0) for d in result['detections']),
        'decode_ms':sum(d.get('decode_ms',0) for d in result['detections']),**other}
   measurements.append(row)
   # Preserve numeric evidence and predictions, without redistributing photographs.
   packed={'id':item['id'],'input_kind':item['kind'],'ground_truth':item['plates'],
           'detections':[{k:v for k,v in d.items() if k!='input_path'} for d in result['detections']],
           'timing':row}
   dst=a.output/'raw_json'/folder.name;dst.mkdir(exist_ok=True,parents=True)
   (dst/(item['id']+'.json')).write_text(json.dumps(packed,ensure_ascii=False,indent=2),encoding='utf-8')
  timing.extend(measurements)
  scope={k:describe([x[k] for x in measurements]) for k in measurements[0] if k.endswith('_ms')}
  scope['model_init_ms']=describe([measurements[0]['model_init_ms']])
  (a.output/(folder.name+'_timing_summary.json')).write_text(json.dumps(scope,indent=2),encoding='utf-8')
 for filename,data in [('image_metrics.csv',rows),('stage_timings.csv',timing)]:
  with (a.output/filename).open('w',newline='',encoding='utf-8') as f:
   w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
 print(json.dumps(rows,indent=2,ensure_ascii=False))
if __name__=='__main__':main()
