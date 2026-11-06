import importlib.util, json, unittest, uuid
from pathlib import Path
spec=importlib.util.spec_from_file_location('evaluate_images',Path(__file__).resolve().parents[1]/'experiments/evaluate_images.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class EvaluationTests(unittest.TestCase):
    def test_one_to_one_matching_and_negatives(self):
        root=Path(__file__).resolve().parents[1]/'.test-output'/uuid.uuid4().hex
        root.mkdir(parents=True)
        if root.is_dir():
            pred=lambda text,box,**kw:dict(raw_plate=text,bbox_xyxy=box,basic_complete=True,review_reason='',**kw)
            cases={'positive':[pred('A',[0,0,100,30]),pred('B',[0,0,100,30])],
                   'negative':[pred('X',[0,0,100,30])],
                   'rejected':[pred('Y',[0,0,100,30],accepted=False)]}
            for ident,d in cases.items():
                (root/ident).mkdir();(root/ident/'results.json').write_text(json.dumps({'detections':d}),encoding='utf-8')
            manifest=[{'id':'positive','plates':[{'text':'A','bbox':[0,0,100,30]}]},
                      {'id':'negative','plates':[]},{'id':'rejected','plates':[]}]
            r=module.evaluate(manifest,root)['metrics']
            self.assertEqual(r['detected_plates'],1);self.assertEqual(r['false_candidates'],2)
            self.assertEqual(r['negative_false_candidates'],1)
            # Tied overlap may match either candidate, but never both to one truth.
            self.assertLessEqual(r['exact_plates'],1)

    def test_empty_truth_has_no_accuracy(self):
        r=module.evaluate([],Path('.'))['metrics']
        self.assertIsNone(r['end_to_end_exact_rate'])

    def test_reject_unsafe_id(self):
        with self.assertRaises(ValueError):module.evaluate([{'id':'../escape','plates':[]}],Path('.'))

if __name__=='__main__':unittest.main()
