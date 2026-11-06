"""Match admitted detections one-to-one to annotated plates; standard library only."""
import argparse, json
from pathlib import Path

def iou(a,b):
    w=max(0,min(a[2],b[2])-max(a[0],b[0]));h=max(0,min(a[3],b[3])-max(a[1],b[1]))
    area=lambda r:max(0,r[2]-r[0])*max(0,r[3]-r[1])
    return w*h/max(1e-12,area(a)+area(b)-w*h)

def evaluate(manifest,results_root):
    stats=dict(images=0,positive_images=0,negative_images=0,ground_truth_plates=0,
               detected_plates=0,missed_plates=0,false_candidates=0,
               exact_plates=0,negative_false_candidates=0,negative_number_outputs=0,review_candidates=0)
    detail=[]
    for item in manifest:
        ident=item['id']
        if not ident or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in ident):
            raise ValueError('IDs must be simple ASCII filenames')
        root=Path(results_root)/ident
        result=json.loads((root/'results.json').read_text(encoding='utf-8'))
        gt=item['plates'];pred=[d for d in result['detections'] if d.get('accepted',True)]
        stats['images']+=1;stats['positive_images' if gt else 'negative_images']+=1
        stats['ground_truth_plates']+=len(gt)
        pairs=sorted(((iou(g['bbox'],d['bbox_xyxy']),gi,di) for gi,g in enumerate(gt) for di,d in enumerate(pred)),reverse=True)
        used_g=set();used_d=set();correct=0
        for score,gi,di in pairs:
            if score<.5:break
            if gi in used_g or di in used_d:continue
            used_g.add(gi);used_d.add(di)
            correct+=pred[di]['raw_plate']==gt[gi]['text'] and not pred[di].get('review_reason')
        stats['detected_plates']+=len(used_g);stats['missed_plates']+=len(gt)-len(used_g)
        stats['false_candidates']+=len(pred)-len(used_d);stats['exact_plates']+=correct
        stats['review_candidates']+=sum(not d.get('basic_complete') or bool(d.get('review_reason')) for d in pred)
        if not gt:
            stats['negative_false_candidates']+=len(pred)
            stats['negative_number_outputs']+=sum(d.get('basic_complete',False) and not d.get('review_reason') for d in pred)
        detail.append(dict(id=ident,matched=len(used_g),missed=len(gt)-len(used_g),false_candidates=len(pred)-len(used_d),exact=correct))
    count=stats['ground_truth_plates'];stats['end_to_end_exact_rate']=stats['exact_plates']/count if count else None
    stats['detection_recall']=stats['detected_plates']/count if count else None
    return {'kind':'annotated_real_image_evaluation','metrics':stats,'images':detail}

def main():
    p=argparse.ArgumentParser();p.add_argument('manifest',type=Path);p.add_argument('results',type=Path);p.add_argument('--output',type=Path)
    a=p.parse_args();r=evaluate(json.loads(a.manifest.read_text(encoding='utf-8')),a.results)
    text=json.dumps(r,indent=2,ensure_ascii=False);print(text)
    if a.output:a.output.write_text(text,encoding='utf-8')
if __name__=='__main__':main()
