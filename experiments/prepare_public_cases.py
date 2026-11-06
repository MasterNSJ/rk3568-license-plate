"""Prepare an explicitly grouped small public-image stress set, not 50 independent photos.
Run with Python + OpenCV. Original source images are not redistributed by this script.
"""
import argparse, hashlib, json
from pathlib import Path
import cv2
import numpy as np

SOURCES=[
 ('Quicker_20220930_180856.png','川A8H458',[422,300,666,425],None),
 ('Quicker_20220930_180919.png','粤NBM959',[838,381,900,422],[360,200,1000,651]),
 ('Quicker_20220930_180938.png','豫ERU586',[181,145,280,183],None),
 ('Quicker_20220930_181044.png','粤B6R678',[350,202,453,245],[345,80,641,331]),
 ('single_blue.jpg','浙B2V9L7',[235,514,303,572],[150,250,900,800]),
 ('single_green.jpg','沪AF71017',[398,518,662,608],None),
 ('single_yellow.jpg','皖K66666',[360,325,489,370],[50,100,630,450]),
]
def main():
 p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
 images=a.output/'images';images.mkdir(parents=True,exist_ok=True);items=[]
 for index,(name,text,bbox,roi) in enumerate(SOURCES):
  image=cv2.imread(str(a.source/name));assert image is not None,name
  if roi:
   x1,y1,x2,y2=roi;image=image[y1:y2,x1:x2];bbox=[bbox[0]-x1,bbox[1]-y1,bbox[2]-x1,bbox[3]-y1]
  transforms=['original','dim','bright','blur','scale70','scale40','rotate5']
  if index==0:transforms.append('rotate_minus5')
  for variant in transforms:
   result=image.copy();box=np.array(bbox,dtype=float)
   if variant=='dim':result=np.clip(result.astype(float)*.6,0,255).astype(np.uint8)
   elif variant=='bright':result=np.clip(result.astype(float)*1.25,0,255).astype(np.uint8)
   elif variant=='blur':result=cv2.GaussianBlur(result,(5,5),1.2)
   elif variant.startswith('scale'):
    factor=int(variant[5:])/100;result=cv2.resize(result,None,fx=factor,fy=factor,interpolation=cv2.INTER_AREA)
    box*=factor
   elif variant.startswith('rotate'):
    h,w=result.shape[:2];angle=-5 if variant.endswith('minus5') else 5
    matrix=cv2.getRotationMatrix2D((w/2,h/2),angle,1)
    result=cv2.warpAffine(result,matrix,(w,h),borderMode=cv2.BORDER_REPLICATE)
    x1,y1,x2,y2=box;points=np.array([[x1,y1,1],[x2,y1,1],[x2,y2,1],[x1,y2,1]])@matrix.T
    box=np.r_[points.min(axis=0),points.max(axis=0)]
   ident=f'p{index+1:02d}_{variant}';dest=images/(ident+'.png');cv2.imwrite(str(dest),result)
   items.append(dict(id=ident,image='images/'+dest.name,source_image=name,group=f'source_{index+1}',
                     variant=variant,kind='public_photo_derived_positive',
                     plates=[dict(text=text,bbox=box.round(2).tolist())],
                     sha256=hashlib.sha256(dest.read_bytes()).hexdigest()))
 for index,src in enumerate(sorted(a.source.iterdir())):
  if src.suffix.lower() not in ('.png','.jpg','.jpeg'):continue
  image=cv2.imread(str(src));h,w=image.shape[:2]
  for side,(x1,x2) in enumerate([(0,int(w*.6)),(int(w*.4),w)]):
   crop=image[:int(h*.22),x1:x2];ident=f'n{index:02d}_{side}';dest=images/(ident+'.png')
   cv2.imwrite(str(dest),crop)
   items.append(dict(id=ident,image='images/'+dest.name,source_image=src.name,variant='top_region_crop',
                     kind='public_photo_negative_crop',plates=[],sha256=hashlib.sha256(dest.read_bytes()).hexdigest()))
 # Deliberately synthetic sign stress cases, reported separately from real image crops.
 for index in range(4):
  canvas=np.full((300,640,3),70+index*30,dtype=np.uint8)
  cv2.rectangle(canvas,(160,90),(480,200),(225,225,225),-1)
  cv2.putText(canvas,'TAXI',(180,173),cv2.FONT_HERSHEY_SIMPLEX,2.5,(10,10,10),5)
  ident=f'synthetic_taxi_{index}';dest=images/(ident+'.png');cv2.imwrite(str(dest),canvas)
  items.append(dict(id=ident,image='images/'+dest.name,source_image=None,variant='rendered_sign',
                    kind='synthetic_taxi_negative',plates=[],sha256=hashlib.sha256(dest.read_bytes()).hexdigest()))
 (a.output/'manifest.json').write_text(json.dumps(items,indent=2,ensure_ascii=False),encoding='utf-8')
 provenance={'upstream':'https://github.com/we0091234/Chinese_license_plate_detection_recognition',
             'source_commit':'670b765c09fff11850a7b1c542787ad66c24ae50',
             'preparation_opencv':cv2.__version__,
             'independent_positive_source_images':7,'positive_derived_cases':50,
             'negative_photo_crops':sum(i['kind']=='public_photo_negative_crop' for i in items),
             'synthetic_negative_signs':4,'annotation':'Manually transcribed text and approximate original-image boxes, then geometrically transformed',
             'limitations':['Small source set; variants are correlated','Negative crops are not independent full-scene photographs',
                            'Synthetic TAXI signs do not establish real taxi roof-sign performance',
                            'No train/validation/test split; this is an exploratory threshold sweep']}
 (a.output/'provenance.json').write_text(json.dumps(provenance,indent=2,ensure_ascii=False),encoding='utf-8')
 print(json.dumps(provenance,ensure_ascii=False))
if __name__=='__main__':main()
