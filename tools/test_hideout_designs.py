"""Catalog and geometric invariants for the authored hideouts (not a live-game claim)."""
import json
import math
import unittest
from tools import build_hideout_designs as design


class HideoutDesigns(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cat=design.catalog()
        cls.data=json.loads((design.ROOT/'ui/hideout-designs.json').read_text(encoding='utf-8'))

    def test_all_models_are_exact_catalog_entries(self):
        self.assertTrue(40<=len(self.data['models'])<=60)
        for model in self.data['models']:
            self.assertEqual(model['guid'],self.cat[model['name']]['guid'])

    def test_designs_have_distinct_shapes_and_focal_models(self):
        rows=self.data['designs']
        self.assertEqual(len(rows),5)
        self.assertGreaterEqual(len({d['shape'] for d in rows}),3)
        self.assertEqual(len({d['label'] for d in rows}),5)
        sets=[{i['guid'] for i in d['items'] if i['door']==-100} for d in rows]
        for a in range(5):
            for b in range(a):self.assertTrue(sets[a]-sets[b] and sets[b]-sets[a])

    def test_all_anchors_and_conservative_decor_envelopes_inside(self):
        for d in self.data['designs']:
            self.assertIn(d['size'],(26,38,55,75))
            self.assertTrue(25<=len(d['items'])<=60)
            for i in d['items']:
                for k in ('right','up','lift','yaw','size'):self.assertTrue(math.isfinite(i[k]))
                margin=design.outline(d['shape'],d['size'],i['right'],i['up'])
                self.assertGreaterEqual(margin,1.5)
                self.assertGreaterEqual(margin-i.get('footprintRadius',i['maxSpan']/math.sqrt(2)),1.499)
                if i['kind']==1:self.assertEqual(i['guid'],self.cat[i['name']]['guid'])

    def test_functional_stations_and_real_game_gate_actions(self):
        for d in self.data['designs']:
            for n in ('Stash','Annya the Blacksmith','Cache_of_Eternity_01','DOOR_RETURN'):
                self.assertEqual(sum(i['name']==n for i in d['items']),1)
            cache=next(i for i in d['items'] if i['name']=='Cache_of_Eternity_01')
            self.assertEqual(cache['size'],.3)
            gates=[i for i in d['items'] if i['name'].startswith('DOOR_') and i['name']!='DOOR_RETURN']
            self.assertEqual(sorted(i['door'] for i in gates),[-1,0]+list(range(1,11)))
            spacing=min(math.hypot(a['right']-b['right'],a['up']-b['up']) for k,a in enumerate(gates) for b in gates[k+1:])
            self.assertGreaterEqual(spacing,8-1e-9)
            self.assertAlmostEqual(spacing,d['planning']['minimumDoorCenterDistance'],places=3)

    def test_inlaid_floors_finish_above_ground_without_raised_slabs(self):
        measured={m['name']:m for m in json.loads((design.ROOT/'ui/hideout-model-metrics.json').read_text(encoding='utf-8'))['models']}
        for d in self.data['designs']:
            floors=[i for i in d['items'] if i.get('surface')]
            self.assertGreaterEqual(len(floors),2)
            for i in floors:
                top=i['lift']+measured[i['name']]['height']*i['size']
                self.assertAlmostEqual(top,i['surfaceTop'],places=5)
                self.assertTrue(i['walkable'])
                self.assertLessEqual(top,.1)
                self.assertFalse(i['fit'])

    def test_every_decor_is_measured_and_curated(self):
        curated={m['name'] for m in self.data['models']}
        for d in self.data['designs']:
            for i in d['items']:
                if i['door']==-100:
                    self.assertIn(i['name'],curated)
                    self.assertFalse(i['fit'])
                    self.assertIn('footprintRadius',i)

    def test_planned_service_and_door_routes_have_three_units_clearance(self):
        from tools.hideout_design_report import paths
        for d in self.data['designs']:
            routes=paths(d)
            self.assertEqual(len(routes),16)
            self.assertGreaterEqual(min(r['plannedClearWidth'] for r in routes),3.0,d['label'])

    def test_placement_exports_use_the_required_seven_columns(self):
        for d in self.data['designs']:
            lines=(design.ROOT/'docs/hideout-designs'/f"{d['id']}.tsv").read_text(encoding='utf-8').splitlines()
            self.assertEqual(lines[0],'ad\tright\tup\tlift\tyaw\tsize\tnot')
            self.assertEqual(len(lines),len(d['items'])+1)
            self.assertTrue(all(len(line.split('\t'))==7 for line in lines))

    def test_island_decor_does_not_occupy_the_causeways(self):
        d=next(d for d in self.data['designs'] if d['shape']=='islands')
        for i in d['items']:
            if 18<abs(i['right'])<23:
                self.assertGreater(abs(i['up'])-i['maxSpan']/math.sqrt(2),3)


if __name__=='__main__':unittest.main()
